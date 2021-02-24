#ifndef HEYP_HOST_AGENT_FLOW_TRACKER_H_
#define HEYP_HOST_AGENT_FLOW_TRACKER_H_

#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/flows/state.h"
#include "heyp/proto/alg.h"

namespace heyp {

enum class FlowPri {
  kUnset,
  kHi,
  kLo,
};

class FlowStateProvider {
 public:
  virtual ~FlowStateProvider() = default;

  virtual void ForEachActiveFlow(
      absl::FunctionRef<void(absl::Time, const proto::FlowInfo&)> func) const = 0;

  virtual void ForEachFlow(
      absl::FunctionRef<void(absl::Time, const proto::FlowInfo&)> func) const = 0;
};

class FlowStateReporter {
 public:
  virtual ~FlowStateReporter() = default;

  virtual absl::Status ReportState(
      absl::FunctionRef<bool(const proto::FlowMarker&)> is_lopri) = 0;
};

class FlowTracker : public FlowStateProvider {
 public:
  struct Config {
    absl::Duration usage_history_window = absl::Seconds(120);
    bool ignore_instantaneous_usage = false;
  };

  FlowTracker(std::unique_ptr<DemandPredictor> demand_predictor, Config config);

  void ForEachActiveFlow(
      absl::FunctionRef<void(absl::Time, const proto::FlowInfo&)> func) const override;

  void ForEachFlow(
      absl::FunctionRef<void(absl::Time, const proto::FlowInfo&)> func) const override;

  struct Update {
    proto::FlowMarker flow;
    int64_t instantaneous_usage_bps;
    int64_t cum_usage_bytes;
    FlowPri used_priority;
  };

  // Updates the usage of the specified Flows.
  // Each Flow should have a zero (i.e. unassigned unique_flow_id) because the
  // FlowTracker will assign one.
  void UpdateFlows(absl::Time timestamp, absl::Span<const Update> flow_update_batch);

  // Updates the usage of the specified Flows and marks them as complete.
  // Each Flow should have a zero (i.e. unassigned unique_flow_id) because the
  // FlowTracker will assign one.
  void FinalizeFlows(absl::Time timestamp, absl::Span<const Update> flow_update_batch);

 private:
  const Config config_;
  const std::unique_ptr<DemandPredictor> demand_predictor_;

  mutable absl::Mutex mu_;
  uint64_t next_seqnum_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<proto::FlowMarker, LeafState, HashHostFlowNoId,
                      EqHostFlowNoId>
      active_flows_
          ABSL_GUARDED_BY(mu_);  // key has zero flow id, value has correct flow id
  std::vector<LeafState> done_flows_ ABSL_GUARDED_BY(mu_);
};

class SSFlowStateReporter : public FlowStateReporter {
 public:
  struct Config {
    uint64_t host_id;
    // my_addrs is a list of addresses that we should report flow state
    // information for. Useful for accounting for both IPv4 and IPv6 addresses
    // on the same interface.
    std::vector<std::string> my_addrs;
    std::string ss_binary_name = "ss";
  };

  ~SSFlowStateReporter();

  static absl::StatusOr<std::unique_ptr<SSFlowStateReporter>> Create(
      Config config, FlowTracker* flow_tracker);

  absl::Status ReportState(
      absl::FunctionRef<bool(const proto::FlowMarker&)> is_lopri) override;

 private:
  bool IgnoreFlow(const proto::FlowMarker& f);
  void MonitorDone();

  SSFlowStateReporter(Config config, FlowTracker* flow_tracker);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_FLOW_TRACKER_H_
