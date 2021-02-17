#ifndef HEYP_HOST_AGENT_DAEMON_H_
#define HEYP_HOST_AGENT_DAEMON_H_

#include <atomic>
#include <cstdint>
#include <thread>

#include "absl/time/time.h"
#include "grpcpp/grpcpp.h"
#include "heyp/flows/aggregator.h"
#include "heyp/flows/dc-mapper.h"
#include "heyp/host-agent/enforcer.h"
#include "heyp/host-agent/flow-tracker.h"
#include "heyp/proto/heyp.grpc.pb.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

class HostDaemon {
 public:
  struct Config {
    uint64_t host_id;
    absl::Duration inform_period = absl::Seconds(2);
  };

  HostDaemon(const std::shared_ptr<grpc::Channel>& channel, Config config,
             DCMapper* dc_mapper, FlowStateProvider* flow_state_provider,
             std::unique_ptr<FlowAggregator> socket_to_host_aggregator,
             FlowStateReporter* flow_state_reporter, HostEnforcer* enforcer);

  ~HostDaemon();

  void Run(std::atomic<bool>* should_exit);

 private:
  const Config config_;
  DCMapper* dc_mapper_;
  FlowStateProvider* flow_state_provider_;
  std::unique_ptr<FlowAggregator> socket_to_host_aggregator_;
  FlowStateReporter* flow_state_reporter_;
  HostEnforcer* enforcer_;
  std::unique_ptr<proto::ClusterAgent::Stub> stub_;

  grpc::ClientContext context_;
  std::unique_ptr<grpc::ClientReaderWriter<proto::InfoBundle, proto::AllocBundle>>
      io_stream_;
  std::thread info_thread_;
  std::thread enforcer_thread_;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_DAEMON_H_
