#include "heyp/host-agent/daemon.h"

#include "absl/time/clock.h"
#include "glog/logging.h"
#include "heyp/proto/constructors.h"

namespace heyp {

HostDaemon::HostDaemon(const std::shared_ptr<grpc::Channel>& channel,
                       Config config, DCMapper* dc_mapper,
                       FlowStateProvider* flow_state_provider,
                       FlowStateReporter* flow_state_reporter,
                       HostEnforcerInterface* enforcer)
    : config_(config),
      dc_mapper_(dc_mapper),
      flow_state_provider_(flow_state_provider),
      flow_state_reporter_(flow_state_reporter),
      enforcer_(enforcer),
      stub_(proto::ClusterAgent::NewStub(channel)) {}

namespace {

proto::FlowMarker WithDCs(proto::FlowMarker marker, const DCMapper& dc_mapper) {
  marker.set_src_dc(dc_mapper.HostDC(marker.src_addr()));
  marker.set_dst_dc(dc_mapper.HostDC(marker.dst_addr()));
  return marker;
}

bool FlaggedOrWaitFor(absl::Duration dur, std::atomic<bool>* exit_flag) {
  absl::Time start = absl::Now();
  absl::Duration piece = std::min(dur / 10, absl::Milliseconds(100));
  while (!exit_flag->load() && absl::Now() - start < dur) {
    absl::SleepFor(piece);
  }
  return exit_flag->load();
}

void SendInfo(absl::Duration inform_period, uint64_t host_id,
              const DCMapper* dc_mapper, FlowStateProvider* flow_state_provider,
              FlowStateReporter* flow_state_reporter,
              std::atomic<bool>* should_exit,
              grpc::ClientReaderWriter<proto::InfoBundle, proto::AllocBundle>*
                  io_stream) {
  do {
    proto::InfoBundle bundle;
    bundle.mutable_bundler()->set_host_id(host_id);
    LOG(INFO) << "TODO: implement HIPRI/LOPRI tracking";
    absl::Status report_status = flow_state_reporter->ReportState(
        [](const proto::FlowMarker&) { return false; });
    if (!report_status.ok()) {
      LOG(ERROR) << "failed to report flow state: " << report_status;
      continue;
    }
    // TODO: aggregate into src_dc, dst_dc, host_id
    *bundle.mutable_timestamp() = ToProtoTimestamp(absl::Now());
    flow_state_provider->ForEachActiveFlow(
        [&bundle, dc_mapper](absl::Time time, const proto::FlowInfo& info) {
          auto send_info = bundle.add_flow_infos();
          *send_info = info;
          *send_info->mutable_flow() = WithDCs(info.flow(), *dc_mapper);
        });
    io_stream->Write(bundle);
  } while (!FlaggedOrWaitFor(inform_period, should_exit));

  io_stream->WritesDone();
}

void EnforceAlloc(const FlowStateProvider* flow_state_provider,
                  HostEnforcerInterface* enforcer,
                  grpc::ClientReaderWriter<proto::InfoBundle,
                                           proto::AllocBundle>* io_stream) {
  while (true) {
    proto::AllocBundle bundle;
    if (!io_stream->Read(&bundle)) {
      break;
    }
    enforcer->EnforceAllocs(*flow_state_provider, bundle);
  }
}

}  // namespace

void HostDaemon::Run(std::atomic<bool>* should_exit) {
  CHECK(io_stream_ == nullptr);

  io_stream_ = stub_->RegisterHost(&context_);

  info_thread_ =
      std::thread(SendInfo, config_.inform_period, config_.host_id, dc_mapper_,
                  flow_state_provider_, flow_state_reporter_, should_exit,
                  io_stream_.get());

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
