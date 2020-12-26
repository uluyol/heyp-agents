#include "heyp/host-agent/flow-tracker.h"

#include <algorithm>

#include "absl/status/status.h"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "boost/process/pipe.hpp"
#include "boost/process/search_path.hpp"
#include "glog/logging.h"

namespace bp = boost::process;

namespace heyp {

struct FlowTrackerInternal {
  boost::process::child monitor_done_proc;
  boost::process::ipstream monitor_done_out;
};

FlowTracker::~FlowTracker() {
  is_dead_.Notify();
  internal_->monitor_done_proc.terminate();

  if (monitor_done_thread_.joinable()) {
    monitor_done_thread_.join();
  }
  if (periodic_snapshot_thread_.joinable()) {
    periodic_snapshot_thread_.join();
  }
}

void FlowTracker::ForEachActiveFlow(
    absl::FunctionRef<void(const FlowState &)> func) {
  absl::MutexLock l(&mu_);
  for (const auto &flow_state_pair : active_flows_) {
    func(flow_state_pair.second);
  }
}

FlowTracker::FlowTracker(std::unique_ptr<DemandPredictor> demand_predictor,
                         Config config)
    : config_(config),
      demand_predictor_(std::move(demand_predictor)),
      internal_(absl::WrapUnique(new FlowTrackerInternal())),
      next_flow_id_(0) {}

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

  while (internal_->monitor_done_proc.running() &&
         std::getline(internal_->monitor_done_out, line) && !line.empty()) {
    absl::Time now = absl::Now();
    Flow f;
    int64_t usage_bps = 0;
    auto status = ParseLine(line, f, usage_bps);
    if (!status.ok()) {
      LOG(ERROR) << "failed to parse done line: " << status;
      continue;
    }

    absl::MutexLock lock(&mu_);
    if (!active_flows_.contains(f)) {
      active_flows_[f] = CreateFlowState(f, ++next_flow_id_);
    }
    UpdateFlowState(now, usage_bps, *demand_predictor_,
                    config_.usage_history_window, active_flows_[f]);
    done_flows_.push_back(active_flows_[f]);
    active_flows_.erase(f);
  }

  CHECK(is_dead_.WaitForNotificationWithTimeout(absl::ZeroDuration()));
}

void FlowTracker::GetSnapshotPeriodically() {
  while (!is_dead_.WaitForNotificationWithTimeout(config_.snapshot_period)) {
    try {
      bp::ipstream out;
      bp::child c(bp::search_path("ss"), "-i", "-t", "-n", "-H", "-O",
                  bp::std_out > out);

      absl::Time now = absl::Now();
      std::string line;
      absl::MutexLock lock(&mu_);
      while (c.running() && std::getline(out, line) && !line.empty()) {
        Flow f;
        int64_t usage_bps = 0;
        auto status = ParseLine(line, f, usage_bps);
        if (!status.ok()) {
          LOG(ERROR) << "failed to parse snapshot line: " << status;
          continue;
        }

        if (!active_flows_.contains(f)) {
          active_flows_[f] = CreateFlowState(f, ++next_flow_id_);
        }
        UpdateFlowState(now, usage_bps, *demand_predictor_,
                        config_.usage_history_window, active_flows_[f]);
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
    bp::child c(bp::search_path("ss"), "-E", "-i", "-t", "-n", "-H", "-O",
                bp::std_out > tracker->internal_->monitor_done_out);
    tracker->internal_->monitor_done_proc = std::move(c);
  } catch (const std::system_error &e) {
    return absl::UnknownError(
        absl::StrCat("failed to start ss subprocess: ", e.what()));
  }

  tracker->monitor_done_thread_ =
      std::thread(&FlowTracker::MonitorDone, tracker.get());
  tracker->periodic_snapshot_thread_ =
      std::thread(&FlowTracker::GetSnapshotPeriodically, tracker.get());

  return tracker;
}

}  // namespace heyp
