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

void SendInfo(
    absl::Duration inform_period, uint64_t host_id, const DCMapper* dc_mapper,
    FlowStateProvider* flow_state_provider,
    FlowStateReporter* flow_state_reporter, std::atomic<bool>* should_exit,
    grpc::ClientReaderWriter<proto::HostInfo, proto::HostAlloc>* io_stream) {
  do {
    proto::HostInfo info;
    info.set_host_id(host_id);
    absl::Status report_status = flow_state_reporter->ReportState();
    if (!report_status.ok()) {
      LOG(ERROR) << "failed to report flow state: " << report_status;
      continue;
    }
    flow_state_provider->ForEachActiveFlow(
        [&info, dc_mapper](const FlowState& state) {
          proto::FlowInfo* flow_info = info.add_flow_infos();
          *flow_info->mutable_marker() = WithDCs(state.flow(), *dc_mapper);
          flow_info->set_cum_usage_bytes(state.cum_usage_bytes());
          flow_info->set_ewma_usage_bps(state.ewma_usage_bps());
          flow_info->set_demand_bps(state.predicted_demand_bps());
          // TODO: track HIPRI usage and LOPRI usage separately
        });
    *info.mutable_timestamp() = ToProtoTimestamp(absl::Now());
    io_stream->Write(info);
  } while (!FlaggedOrWaitFor(inform_period, should_exit));

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
