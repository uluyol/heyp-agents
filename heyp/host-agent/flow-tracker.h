#ifndef HEYP_HOST_AGENT_FLOW_TRACKER_H_
#define HEYP_HOST_AGENT_FLOW_TRACKER_H_

#include <memory>
#include <thread>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/host-agent/flow.h"

namespace heyp {

struct FlowState {
  Flow flow;

  int64_t predicted_demand_bps = 0;
  int64_t ewma_usage_bps = 0;
  std::vector<UsageHistoryEntry> usage_history;
};

class FlowTracker {
 public:
  struct Config {
    std::string ss_binary_name = "ss";
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

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_FLOW_TRACKER_H_
