#include "heyp/host-agent/flow-tracker.h"

#include <algorithm>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "boost/process/pipe.hpp"
#include "boost/process/search_path.hpp"
#include "glog/logging.h"

namespace bp = boost::process;

namespace heyp {

struct FlowTracker::Impl {
  const Config config;
  const std::unique_ptr<DemandPredictor> demand_predictor;

  bp::child monitor_done_proc;
  bp::ipstream monitor_done_out;

  absl::Notification is_dead;
  std::thread monitor_done_thread;
  std::thread periodic_snapshot_thread;

  absl::Mutex mu;
  uint64_t next_flow_id ABSL_GUARDED_BY(mu);
  absl::flat_hash_map<Flow, FlowState> active_flows
      ABSL_GUARDED_BY(mu);  // key has zero flow id, value has correct flow id
  std::vector<FlowState> done_flows ABSL_GUARDED_BY(mu);
};

FlowTracker::~FlowTracker() {
  impl_->is_dead.Notify();
  impl_->monitor_done_proc.terminate();

  if (impl_->monitor_done_thread.joinable()) {
    impl_->monitor_done_thread.join();
  }
  if (impl_->periodic_snapshot_thread.joinable()) {
    impl_->periodic_snapshot_thread.join();
  }
}

void FlowTracker::ForEachActiveFlow(
    absl::FunctionRef<void(const FlowState &)> func) {
  absl::MutexLock l(&impl_->mu);
  for (const auto &flow_state_pair : impl_->active_flows) {
    func(flow_state_pair.second);
  }
}

FlowTracker::FlowTracker(std::unique_ptr<DemandPredictor> demand_predictor,
                         Config config)
    : impl_(absl::WrapUnique(new Impl{
          .config = config,
          .demand_predictor = std::move(demand_predictor),
          .next_flow_id = 0,
      })) {}

namespace {
absl::Status ParseLine(absl::string_view line, Flow &parsed,
                       int64_t &usage_bps) {
  LOG(FATAL) << "not implemented";
}

FlowState CreateFlowState(const Flow &f, uint64_t id) {
  auto fs = FlowState{.flow = f};
  fs.flow.unique_id = id;
  return fs;
}

void UpdateFlowState(absl::Time now, int64_t usage_bps,
                     const DemandPredictor &demand_predictor,
                     absl::Duration time_window, FlowState &fs) {
  bool is_first_entry = fs.usage_history.empty();
  fs.usage_history.push_back({now, usage_bps});

  // Garbage collect old entries, but allow some delay.
  if (now - fs.usage_history.front().time > 2 * time_window) {
    absl::Time min_time = now - time_window;
    auto first_to_keep =
        std::find_if(fs.usage_history.begin(), fs.usage_history.end(),
                     [min_time](const UsageHistoryEntry &entry) {
                       return entry.time >= min_time;
                     });
    size_t n = std::distance(fs.usage_history.begin(), first_to_keep);
    std::move(first_to_keep, fs.usage_history.end(), fs.usage_history.begin());
    fs.usage_history.resize(n);
  }

  fs.predicted_demand_bps = demand_predictor.FromUsage(now, fs.usage_history);

  if (is_first_entry) {
    fs.ewma_usage_bps = usage_bps;
  } else {
    constexpr double alpha = 0.3;
    fs.ewma_usage_bps = alpha * (usage_bps) + (1 - alpha) * fs.ewma_usage_bps;
  }
}

}  // namespace

void FlowTracker::MonitorDone() {
  std::string line;

  while (impl_->monitor_done_proc.running() &&
         std::getline(impl_->monitor_done_out, line) && !line.empty()) {
    absl::Time now = absl::Now();
    Flow f;
    int64_t usage_bps = 0;
    auto status = ParseLine(line, f, usage_bps);
    if (!status.ok()) {
      LOG(ERROR) << "failed to parse done line: " << status;
      continue;
    }

    absl::MutexLock lock(&impl_->mu);
    if (!impl_->active_flows.contains(f)) {
      impl_->active_flows[f] = CreateFlowState(f, ++impl_->next_flow_id);
    }
    UpdateFlowState(now, usage_bps, *impl_->demand_predictor,
                    impl_->config.usage_history_window, impl_->active_flows[f]);
    impl_->done_flows.push_back(impl_->active_flows[f]);
    impl_->active_flows.erase(f);
  }

  CHECK(impl_->is_dead.WaitForNotificationWithTimeout(absl::ZeroDuration()));
}

void FlowTracker::GetSnapshotPeriodically() {
  while (!impl_->is_dead.WaitForNotificationWithTimeout(
      impl_->config.snapshot_period)) {
    try {
      bp::ipstream out;
      bp::child c(bp::search_path(impl_->config.ss_binary_name), "-i", "-t",
                  "-n", "-H", "-O", bp::std_out > out);

      absl::Time now = absl::Now();
      std::string line;
      absl::MutexLock lock(&impl_->mu);
      while (c.running() && std::getline(out, line) && !line.empty()) {
        Flow f;
        int64_t usage_bps = 0;
        auto status = ParseLine(line, f, usage_bps);
        if (!status.ok()) {
          LOG(ERROR) << "failed to parse snapshot line: " << status;
          continue;
        }

        if (!impl_->active_flows.contains(f)) {
          impl_->active_flows[f] = CreateFlowState(f, ++impl_->next_flow_id);
        }
        UpdateFlowState(now, usage_bps, *impl_->demand_predictor,
                        impl_->config.usage_history_window,
                        impl_->active_flows[f]);
      }
      c.wait();
    } catch (const std::system_error &e) {
      LOG(ERROR) << "failed to get snapshot: "
                 << absl::UnknownError(absl::StrCat(
                        "failed to start ss subprocess: ", e.what()));
    }
  }
}

absl::StatusOr<std::unique_ptr<FlowTracker>> FlowTracker::Create(
    std::unique_ptr<DemandPredictor> demand_predictor, Config config) {
  auto tracker =
      absl::WrapUnique(new FlowTracker(std::move(demand_predictor), config));

  try {
    bp::child c(bp::search_path(config.ss_binary_name), "-E", "-i", "-t", "-n",
                "-H", "-O", bp::std_out > tracker->impl_->monitor_done_out);
    tracker->impl_->monitor_done_proc = std::move(c);
  } catch (const std::system_error &e) {
    return absl::UnknownError(
        absl::StrCat("failed to start ss subprocess: ", e.what()));
  }

  tracker->impl_->monitor_done_thread =
      std::thread(&FlowTracker::MonitorDone, tracker.get());
  tracker->impl_->periodic_snapshot_thread =
      std::thread(&FlowTracker::GetSnapshotPeriodically, tracker.get());

  return tracker;
}

}  // namespace heyp
