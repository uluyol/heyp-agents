#ifndef HEYP_CLUSTER_AGENT_SERVER_H_
#define HEYP_CLUSTER_AGENT_SERVER_H_

#include "heyp/cluster-agent/controller.h"
#include "heyp/proto/heyp.grpc.pb.h"

namespace heyp {

class ClusterAgentService final : public proto::ClusterAgent::Service {
 public:
  ClusterAgentService(std::unique_ptr<FlowAggregator> aggregator,
                      std::unique_ptr<ClusterAllocator> allocator,
                      absl::Duration control_period = absl::Seconds(5));

  grpc::Status RegisterHost(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<proto::AllocBundle, proto::InfoBundle>* stream)
      override;

 private:
  const absl::Duration control_period_;
  ClusterController controller_;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_SERVER_H_