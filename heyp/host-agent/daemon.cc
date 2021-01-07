#include "heyp/host-agent/daemon.h"

#include "glog/logging.h"

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
    FlowStateProvider* flow_state_provider, absl::Notification* should_exit,
    grpc::ClientReaderWriter<proto::HostInfo, proto::HostAlloc>* io_stream) {
  // TODO loop
  proto::HostInfo info;
  io_stream->Write(info);
}

void EnforceAlloc(
    HostEnforcerInterface* enforcer, absl::Notification* should_exit,
    grpc::ClientReaderWriter<proto::HostInfo, proto::HostAlloc>* io_stream) {
  proto::HostAlloc alloc;
  io_stream->Read(&alloc);
}

}  // namespace

void HostDaemon::Run(absl::Notification* should_exit) {
  CHECK(io_stream_ == nullptr);

  io_stream_ = stub_->RegisterHost(&context_);

  info_thread_ = std::thread(SendInfo, flow_state_provider_, should_exit,
                             io_stream_.get());

  enforcer_thread_ =
      std::thread(EnforceAlloc, enforcer_, should_exit, io_stream_.get());
}

HostDaemon::~HostDaemon() {
  info_thread_.join();
  enforcer_thread_.join();
}

}  // namespace heyp
