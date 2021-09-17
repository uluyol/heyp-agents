#ifndef HEYP_HOST_AGENT_DAEMON_H_
#define HEYP_HOST_AGENT_DAEMON_H_

#include <atomic>
#include <cstdint>
#include <thread>

#include "absl/time/time.h"
#include "grpcpp/grpcpp.h"
#include "heyp/flows/aggregator.h"
#include "heyp/flows/dc-mapper.h"
#include "heyp/host-agent/cluster-agent-channel.h"
#include "heyp/host-agent/enforcer.h"
#include "heyp/host-agent/flow-tracker.h"
#include "heyp/proto/heyp.grpc.pb.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/proto/ndjson-logger.h"

namespace heyp {

class LogTime;

class HostDaemon {
 public:
  struct Config {
    std::string job_name;
    uint64_t host_id;
    absl::Duration inform_period = absl::Seconds(2);
    absl::Duration collect_stats_period = absl::Milliseconds(500);
    std::string stats_log_file;
    std::string fine_grained_stats_log_file;
  };

  HostDaemon(const std::shared_ptr<grpc::Channel>& channel, Config config,
             DCMapper* dc_mapper, FlowStateProvider* flow_state_provider,
             std::unique_ptr<FlowAggregator> socket_to_host_aggregator,
             FlowStateReporter* flow_state_reporter, HostEnforcer* enforcer);

  ~HostDaemon();

  void Run(std::atomic<bool>* should_exit);

 private:
  // Daemon loops
  void CollectStats(absl::Duration period, bool force_run,
                    NdjsonLogger* flow_state_logger,
                    NdjsonLogger* fine_grained_flow_state_logger,
                    std::atomic<bool>* should_exit,
                    std::shared_ptr<LogTime> last_enforcer_log_time);
  void SendInfos(std::atomic<bool>* should_exit);
  void EnforceAllocs(std::atomic<bool>* should_exit,
                     std::shared_ptr<LogTime> last_enforcer_log_time);

  const Config config_;
  DCMapper* dc_mapper_;
  FlowStateProvider* flow_state_provider_;
  std::unique_ptr<FlowAggregator> socket_to_host_aggregator_;
  FlowStateReporter* flow_state_reporter_;
  NdjsonLogger flow_state_logger_;
  NdjsonLogger fine_grained_flow_state_logger_;
  HostEnforcer* enforcer_;
  ClusterAgentChannel channel_;

  std::thread collect_stats_thread_;
  std::thread info_thread_;
  std::thread enforcer_thread_;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_DAEMON_H_
