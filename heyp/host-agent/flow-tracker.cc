#include "heyp/host-agent/flow-tracker.h"

#include <algorithm>

#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "boost/process/pipe.hpp"
#include "boost/process/search_path.hpp"
#include "glog/logging.h"

namespace bp = boost::process;

namespace heyp {

FlowTracker::FlowTracker(std::unique_ptr<DemandPredictor> demand_predictor,
                         Config config)
    : config_(config),
      demand_predictor_(std::move(demand_predictor)),
      next_flow_id_(0) {}

void FlowTracker::ForEachActiveFlow(
    absl::FunctionRef<void(const FlowState &)> func) const {
  absl::MutexLock l(&mu_);
  for (const auto &flow_state_pair : active_flows_) {
    func(flow_state_pair.second);
  }
}

namespace {

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

void FlowTracker::UpdateFlows(
    absl::Time timestamp,
    absl::Span<const std::pair<Flow, int64_t>> flow_usage_bps_batch) {
  absl::MutexLock lock(&mu_);
  for (const auto &pair : flow_usage_bps_batch) {
    const Flow &f = pair.first;
    if (!active_flows_.contains(f)) {
      active_flows_[f] = CreateFlowState(f, ++next_flow_id_);
    }
    UpdateFlowState(timestamp, pair.second, *demand_predictor_,
                    config_.usage_history_window, active_flows_[f]);
  }
}

void FlowTracker::FinalizeFlows(
    absl::Time timestamp,
    absl::Span<const std::pair<Flow, int64_t>> flow_usage_bps_batch) {
  absl::MutexLock lock(&mu_);
  for (const auto &pair : flow_usage_bps_batch) {
    const Flow &f = pair.first;
    if (!active_flows_.contains(f)) {
      active_flows_[f] = CreateFlowState(f, ++next_flow_id_);
    }
    UpdateFlowState(timestamp, pair.second, *demand_predictor_,
                    config_.usage_history_window, active_flows_[f]);
    done_flows_.push_back(active_flows_[f]);
    active_flows_.erase(f);
  }
}

struct SSFlowStateReporter::Impl {
  const Config config;
  FlowTracker *flow_tracker;

  bp::child monitor_done_proc;
  bp::ipstream monitor_done_out;

  absl::Notification is_dead;
  std::thread monitor_done_thread;
  std::thread periodic_snapshot_thread;
};

SSFlowStateReporter::~SSFlowStateReporter() {
  impl_->is_dead.Notify();
  impl_->monitor_done_proc.terminate();

  if (impl_->monitor_done_thread.joinable()) {
    impl_->monitor_done_thread.join();
  }
  if (impl_->periodic_snapshot_thread.joinable()) {
    impl_->periodic_snapshot_thread.join();
  }
}

namespace {

absl::Status ParseLine(absl::string_view line, Flow &parsed,
                       int64_t &usage_bps) {
  LOG(FATAL) << "not implemented";
}

}  // namespace

void SSFlowStateReporter::MonitorDone() {
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

    impl_->flow_tracker->FinalizeFlows(now, {{f, usage_bps}});
  }

  CHECK(impl_->is_dead.WaitForNotificationWithTimeout(absl::ZeroDuration()));
}

void SSFlowStateReporter::GetSnapshotPeriodically() {
  while (!impl_->is_dead.WaitForNotificationWithTimeout(
      impl_->config.snapshot_period)) {
    try {
      bp::ipstream out;
      bp::child c(bp::search_path(impl_->config.ss_binary_name), "-i", "-t",
                  "-n", "-H", "-O", bp::std_out > out);

      absl::Time now = absl::Now();
      std::string line;
      std::vector<std::pair<Flow, int64_t>> flow_usage_bps;
      while (c.running() && std::getline(out, line) && !line.empty()) {
        Flow f;
        int64_t usage_bps = 0;
        auto status = ParseLine(line, f, usage_bps);
        if (!status.ok()) {
          LOG(ERROR) << "failed to parse snapshot line: " << status;
          continue;
        }
        flow_usage_bps.push_back({f, usage_bps});
      }
      impl_->flow_tracker->UpdateFlows(now, flow_usage_bps);
      c.wait();
    } catch (const std::system_error &e) {
      LOG(ERROR) << "failed to get snapshot: "
                 << absl::UnknownError(absl::StrCat(
                        "failed to start ss subprocess: ", e.what()));
    }
  }
}

SSFlowStateReporter::SSFlowStateReporter(Config config,
                                         FlowTracker *flow_tracker)
    : impl_(absl::WrapUnique(new Impl{
          .config = config,
          .flow_tracker = flow_tracker,
      })) {}

absl::StatusOr<std::unique_ptr<SSFlowStateReporter>>
SSFlowStateReporter::Create(Config config, FlowTracker *flow_tracker) {
  auto tracker =
      absl::WrapUnique(new SSFlowStateReporter(config, flow_tracker));

  try {
    bp::child c(bp::search_path(config.ss_binary_name), "-E", "-i", "-t", "-n",
                "-H", "-O", bp::std_out > tracker->impl_->monitor_done_out);
    tracker->impl_->monitor_done_proc = std::move(c);
  } catch (const std::system_error &e) {
    return absl::UnknownError(
        absl::StrCat("failed to start ss subprocess: ", e.what()));
  }

  tracker->impl_->monitor_done_thread =
      std::thread(&SSFlowStateReporter::MonitorDone, tracker.get());
  tracker->impl_->periodic_snapshot_thread =
      std::thread(&SSFlowStateReporter::GetSnapshotPeriodically, tracker.get());

  return tracker;
}

}  // namespace heyp
