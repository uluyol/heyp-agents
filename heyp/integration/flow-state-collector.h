#ifndef HEYP_INTEGRATION_FLOW_STATE_COLLECTOR_H_
#define HEYP_INTEGRATION_FLOW_STATE_COLLECTOR_H_

#include <thread>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "heyp/host-agent/flow-tracker.h"
#include "heyp/integration/step-worker.h"
#include "heyp/proto/integration.pb.h"

namespace heyp {
namespace testing {

class FlowStateCollector {
 public:
  static absl::StatusOr<std::unique_ptr<FlowStateCollector>> Create(
      const std::vector<HostWorker::Flow>& all_flows, absl::Duration period,
      bool ignore_instantaneous_usage);

  void CollectStep(const std::string& label);
  std::vector<proto::TestCompareMetrics::Metric> Finish();

 private:
  FlowStateCollector();
  std::unique_ptr<FlowTracker> flow_tracker_;
  std::unique_ptr<SSFlowStateReporter> reporter_;

  std::thread report_thread_;
  absl::Notification done_;

  absl::flat_hash_map<std::pair<int, int>, std::string> src_dst_port_to_name_;
  std::vector<proto::TestCompareMetrics::Metric> measurements_;
};

}  // namespace testing
}  // namespace heyp

#endif  // HEYP_INTEGRATION_FLOW_STATE_COLLECTOR_H_
