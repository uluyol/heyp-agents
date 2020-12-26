#ifndef HEYP_HOST_AGENT_FLOW_TRACKER_H_
#define HEYP_HOST_AGENT_FLOW_TRACKER_H_

#include <memory>
#include <thread>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/host-agent/flow.h"

namespace heyp {

struct FlowState {
  Flow flow;

  int64_t predicted_demand_bps = 0;
  int64_t ewma_usage_bps = 0;
  std::vector<UsageHistoryEntry> usage_history;
};

struct FlowTrackerInternal;

class FlowTracker {
 public:
  struct Config {
    absl::Duration snapshot_period = absl::Seconds(5);
    absl::Duration usage_history_window = absl::Seconds(120);
  };

  ~FlowTracker();

  static absl::StatusOr<std::unique_ptr<FlowTracker>> Create(
      std::unique_ptr<DemandPredictor> demand_predictor, Config config);

  void ForEachActiveFlow(absl::FunctionRef<void(const FlowState&)> func);

 private:
  FlowTracker(std::unique_ptr<DemandPredictor> demand_predictor, Config config);

  void MonitorDone();
  void GetSnapshotPeriodically();

  const Config config_;
  std::unique_ptr<DemandPredictor> demand_predictor_;
  std::unique_ptr<FlowTrackerInternal> internal_;

  absl::Notification is_dead_;
  std::thread monitor_done_thread_;
  std::thread periodic_snapshot_thread_;

  absl::Mutex mu_;
  uint64_t next_flow_id_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<Flow, FlowState> active_flows_
      ABSL_GUARDED_BY(mu_);  // key has zero flow id, value has correct flow id
  std::vector<FlowState> done_flows_ ABSL_GUARDED_BY(mu_);
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_FLOW_TRACKER_H_
