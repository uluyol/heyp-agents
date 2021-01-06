
#include "heyp/host-agent/daemon.h"

#include "glog/logging.h"

namespace heyp {

HostDaemon::HostDaemon(std::unique_ptr<FlowTracker> flow_tracker,
                       std::unique_ptr<HostEnforcer> enforcer,
                       const std::shared_ptr<grpc::Channel>& channel,
                       Config config)
    : config_(config),
      flow_tracker_(std::move(flow_tracker)),
      enforcer_(std::move(enforcer)),
      stub_(proto::ClusterAgent::NewStub(channel)) {}

namespace {

void SendInfo(
    FlowTracker* flow_tracker, absl::Notification* should_exit,
    grpc::ClientReaderWriter<proto::HostInfo, proto::HostAlloc>* io_stream) {
  // TODO loop
  proto::HostInfo info;
  io_stream->Write(info);
}

void EnforceAlloc(
    HostEnforcer* enforcer, absl::Notification* should_exit,
    grpc::ClientReaderWriter<proto::HostInfo, proto::HostAlloc>* io_stream) {
  proto::HostAlloc alloc;
  io_stream->Read(&alloc);
}

}  // namespace

void HostDaemon::Run(absl::Notification* should_exit) {
  CHECK(io_stream_ == nullptr);

  io_stream_ = stub_->RegisterHost(&context_);

  info_thread_ =
      std::thread(SendInfo, flow_tracker_.get(), should_exit, io_stream_.get());

  enforcer_thread_ =
      std::thread(EnforceAlloc, enforcer_.get(), should_exit, io_stream_.get());
}

HostDaemon::~HostDaemon() {
  info_thread_.join();
  enforcer_thread_.join();
}

}  // namespace heyp
