#include "heyp/cluster-agent/server.h"

#include "absl/strings/str_format.h"
#include "grpcpp/grpcpp.h"
#include "heyp/cluster-agent/controller.h"
#include "heyp/log/spdlog.h"

namespace heyp {

ClusterAgentService::ClusterAgentService(std::unique_ptr<FlowAggregator> aggregator,
                                         std::unique_ptr<ClusterAllocator> allocator,
                                         absl::Duration control_period)
    : control_period_(control_period),
      controller_(std::move(aggregator), std::move(allocator)),
      logger_(MakeLogger("cluster-agent-svc")) {}

grpc::Status ClusterAgentService::RegisterHost(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<proto::AllocBundle, proto::InfoBundle>* stream) {
  bool registered = false;
  ClusterController::Listener lis;
  SPDLOG_LOGGER_INFO(&logger_, "{}: called by {}", __func__, context->peer());

  std::string peer = context->peer();
  while (true) {
    proto::InfoBundle info;
    if (!stream->Read(&info)) {
      break;
    }
    SPDLOG_LOGGER_INFO(&logger_, "got info from {} with {} FGs", peer,
                       info.flow_infos_size());

    if (!registered) {
      lis = controller_.RegisterListener(
          info.bundler().host_id(), [stream, peer, this](proto::AllocBundle alloc) {
            SPDLOG_LOGGER_INFO(&logger_, "sending allocs for {} FGs to {}",
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
    SPDLOG_LOGGER_INFO(&logger_, "{}: compute new allocations", __func__);
    controller_.ComputeAndBroadcast();
    absl::SleepFor(control_period_);
  }
}

}  // namespace heyp
