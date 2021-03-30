#include "heyp/cluster-agent/server.h"

#include "absl/strings/str_format.h"
#include "glog/logging.h"
#include "grpcpp/grpcpp.h"
#include "heyp/cluster-agent/controller.h"

namespace heyp {

ClusterAgentService::ClusterAgentService(std::unique_ptr<FlowAggregator> aggregator,
                                         std::unique_ptr<ClusterAllocator> allocator,
                                         absl::Duration control_period)
    : control_period_(control_period),
      controller_(std::move(aggregator), std::move(allocator)) {}

grpc::Status ClusterAgentService::RegisterHost(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<proto::AllocBundle, proto::InfoBundle>* stream) {
  bool registered = false;
  ClusterController::Listener lis;
  LOG(INFO) << absl::StrFormat("%s: called by %s", __func__, context->peer());

  std::string peer = context->peer();
  while (true) {
    proto::InfoBundle info;
    if (!stream->Read(&info)) {
      break;
    }
    LOG(INFO) << absl::StrFormat("got info from %s with %d FGs", peer,
                                 info.flow_infos_size());

    if (!registered) {
      lis = controller_.RegisterListener(
          info.bundler().host_id(), [stream, peer](proto::AllocBundle alloc) {
            LOG(INFO) << absl::StrFormat("sending allocs for %d FGs to %s",
                                         alloc.flow_allocs_size(), peer);
            stream->Write(alloc);
          });
    }
    controller_.UpdateInfo(info);
  }
  return grpc::Status::OK;
}

void ClusterAgentService::RunLoop(std::atomic<bool>* should_exit) {
  while (!should_exit->load()) {
    LOG(INFO) << __func__ << ": compute new allocations";
    controller_.ComputeAndBroadcast();
    absl::SleepFor(control_period_);
  }
}

}  // namespace heyp
