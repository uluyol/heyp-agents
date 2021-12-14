#ifndef HEYP_CLUSTER_AGENT_SERVER_H_
#define HEYP_CLUSTER_AGENT_SERVER_H_

#include <atomic>

#include "heyp/cluster-agent/controller-iface.h"
#include "heyp/proto/heyp.grpc.pb.h"
#include "spdlog/spdlog.h"

namespace heyp {

class ClusterAgentService final : public proto::ClusterAgent::CallbackService {
 public:
  ClusterAgentService(std::unique_ptr<ClusterController> controller,
                      absl::Duration control_period = absl::Seconds(5));

  grpc::ServerBidiReactor<proto::InfoBundle, proto::AllocBundle>* RegisterHost(
      grpc::CallbackServerContext* context) override;

  void RunLoop(std::atomic<bool>* should_exit);

 private:
  const absl::Duration control_period_;
  std::unique_ptr<ClusterController> controller_;
  spdlog::logger logger_;

  friend class HostReactor;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_SERVER_H_
