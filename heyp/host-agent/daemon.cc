#include "heyp/host-agent/daemon.h"

#include "glog/logging.h"
#include "heyp/proto/constructors.h"

namespace heyp {

HostDaemon::HostDaemon(const std::shared_ptr<grpc::Channel>& channel,
                       Config config, FlowStateProvider* flow_state_provider,
                       HostEnforcerInterface* enforcer)
    : config_(config),
      flow_state_provider_(flow_state_provider),
      enforcer_(enforcer),
      stub_(proto::ClusterAgent::NewStub(channel)) {}

namespace {

void SendInfo(
    absl::Duration inform_period, const std::string& host_addr,
    FlowStateProvider* flow_state_provider, absl::Notification* should_exit,
    grpc::ClientReaderWriter<proto::HostInfo, proto::HostAlloc>* io_stream) {
  do {
    proto::HostInfo info;
    info.set_addr(host_addr);
    flow_state_provider->ForEachActiveFlow([&info](const FlowState& state) {
      proto::FlowInfo* flow_info = info.add_flow_infos();
      *flow_info->mutable_marker() =
          state.flow();  // TODO: where do we classify?
      flow_info->set_cum_usage_bytes(state.cum_usage_bytes());
      flow_info->set_usage_bps(state.ewma_usage_bps());
      flow_info->set_demand_bps(state.predicted_demand_bps());
    });
    *info.mutable_timestamp() = ToProtoTimestamp(absl::Now());
    io_stream->Write(info);
  } while (!should_exit->WaitForNotificationWithTimeout(inform_period));

  io_stream->WritesDone();
}

void EnforceAlloc(
    const FlowStateProvider* flow_state_provider,
    HostEnforcerInterface* enforcer,
    grpc::ClientReaderWriter<proto::HostInfo, proto::HostAlloc>* io_stream) {
  while (true) {
    proto::HostAlloc alloc;
    if (!io_stream->Read(&alloc)) {
      break;
    }

    enforcer->EnforceAllocs(*flow_state_provider, alloc);
  }
}

}  // namespace

void HostDaemon::Run(absl::Notification* should_exit) {
  CHECK(io_stream_ == nullptr);

  io_stream_ = stub_->RegisterHost(&context_);

  info_thread_ =
      std::thread(SendInfo, config_.inform_period, "TODO_FILL_OUT",
                  flow_state_provider_, should_exit, io_stream_.get());

  enforcer_thread_ = std::thread(EnforceAlloc, flow_state_provider_, enforcer_,
                                 io_stream_.get());
}

HostDaemon::~HostDaemon() {
  if (info_thread_.joinable()) {
    info_thread_.join();
  }
  if (enforcer_thread_.joinable()) {
    enforcer_thread_.join();
  }
}

}  // namespace heyp
