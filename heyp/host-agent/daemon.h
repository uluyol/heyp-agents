#ifndef HEYP_HOST_AGENT_DAEMON_H_
#define HEYP_HOST_AGENT_DAEMON_H_

#include <thread>

#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "grpcpp/grpcpp.h"
#include "heyp/host-agent/enforcer.h"
#include "heyp/host-agent/flow-tracker.h"
#include "heyp/proto/heyp.grpc.pb.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

class HostDaemon {
 public:
  struct Config {
    absl::Duration inform_period = absl::Seconds(2);
  };

  HostDaemon(std::unique_ptr<FlowTracker> flow_tracker,
             std::unique_ptr<HostEnforcer> enforcer,
             const std::shared_ptr<grpc::Channel>& channel, Config config);

  ~HostDaemon();

  void Run(absl::Notification* should_exit);

 private:
  const Config config_;
  std::unique_ptr<FlowTracker> flow_tracker_;
  std::unique_ptr<HostEnforcer> enforcer_;
  std::unique_ptr<proto::ClusterAgent::Stub> stub_;

  grpc::ClientContext context_;
  std::unique_ptr<grpc::ClientReaderWriter<proto::HostInfo, proto::HostAlloc>>
      io_stream_;
  std::thread info_thread_;
  std::thread enforcer_thread_;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_DAEMON_H_
