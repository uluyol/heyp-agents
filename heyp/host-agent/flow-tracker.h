#ifndef HEYP_HOST_AGENT_FLOW_TRACKER_H_
#define HEYP_HOST_AGENT_FLOW_TRACKER_H_

#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/flows/state.h"
#include "heyp/proto/alg.h"

namespace heyp {

class FlowStateProvider {
 public:
  ~FlowStateProvider() = default;

  virtual void ForEachActiveFlow(
      absl::FunctionRef<void(const FlowState&)> func) const = 0;

  virtual void ForEachFlow(
      absl::FunctionRef<void(const FlowState&)> func) const = 0;
};

class FlowTracker : public FlowStateProvider {
 public:
  struct Config {
    absl::Duration usage_history_window = absl::Seconds(120);
  };

  FlowTracker(std::unique_ptr<DemandPredictor> demand_predictor, Config config);

  void ForEachActiveFlow(
      absl::FunctionRef<void(const FlowState&)> func) const override;

  void ForEachFlow(
      absl::FunctionRef<void(const FlowState&)> func) const override;

  // Updates the usage of the specified Flows.
  // Each Flow should have a zero (i.e. unassigned unique_flow_id) because the
  // FlowTracker will assign one.
  void UpdateFlows(absl::Time timestamp,
                   absl::Span<const std::pair<proto::FlowMarker, int64_t>>
                       flow_usage_bytes_batch);

  // Updates the usage of the specified Flows and marks them as complete.
  // Each Flow should have a zero (i.e. unassigned unique_flow_id) because the
  // FlowTracker will assign one.
  void FinalizeFlows(absl::Time timestamp,
                     absl::Span<const std::pair<proto::FlowMarker, int64_t>>
                         flow_usage_bytes_batch);

 private:
  const Config config_;
  const std::unique_ptr<DemandPredictor> demand_predictor_;

  mutable absl::Mutex mu_;
  uint64_t next_flow_id_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<proto::FlowMarker, FlowState, HashHostFlowNoId,
                      EqHostFlowNoId>
      active_flows_ ABSL_GUARDED_BY(
          mu_);  // key has zero flow id, value has correct flow id
  std::vector<FlowState> done_flows_ ABSL_GUARDED_BY(mu_);
};

class SSFlowStateReporter {
 public:
  struct Config {
    std::string ss_binary_name = "ss";
    absl::Duration snapshot_period = absl::Seconds(5);
  };

  ~SSFlowStateReporter();

  static absl::StatusOr<std::unique_ptr<SSFlowStateReporter>> Create(
      Config config, FlowTracker* flow_tracker);

 private:
  void MonitorDone();
  void GetSnapshotPeriodically();

  SSFlowStateReporter(Config config, FlowTracker* flow_tracker);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_FLOW_TRACKER_H_
