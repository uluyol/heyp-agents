#ifndef HEYP_CLUSTER_AGENT_SERVER_H_
#define HEYP_CLUSTER_AGENT_SERVER_H_

#include <atomic>
#include <memory>

#include "heyp/cluster-agent/controller-iface.h"
#include "heyp/proto/heyp.grpc.pb.h"
#include "spdlog/spdlog.h"

namespace heyp {

class ClusterAgentService final : public proto::ClusterAgent::CallbackService {
 public:
  ClusterAgentService(const std::shared_ptr<ClusterController>& controller, int id);

  grpc::ServerBidiReactor<proto::InfoBundle, proto::AllocBundle>* RegisterHost(
      grpc::CallbackServerContext* context) override;

 private:
  std::shared_ptr<ClusterController> controller_;
  spdlog::logger logger_;

  friend class HostReactor;
};

void RunLoop(const std::shared_ptr<ClusterController>& controller,
             absl::Duration control_period, std::atomic<bool>* should_exit,
             spdlog::logger* logger);

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_SERVER_H_
