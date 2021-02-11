#include "heyp/host-agent/daemon.h"

#include "absl/time/clock.h"
#include "glog/logging.h"
#include "heyp/proto/constructors.h"

namespace heyp {

HostDaemon::HostDaemon(
    const std::shared_ptr<grpc::Channel>& channel, Config config,
    DCMapper* dc_mapper, FlowStateProvider* flow_state_provider,
    std::unique_ptr<FlowAggregator> socket_to_host_aggregator,
    FlowStateReporter* flow_state_reporter, HostEnforcerInterface* enforcer)
    : config_(config),
      dc_mapper_(dc_mapper),
      flow_state_provider_(flow_state_provider),
      socket_to_host_aggregator_(std::move(socket_to_host_aggregator)),
      flow_state_reporter_(flow_state_reporter),
      enforcer_(enforcer),
      stub_(proto::ClusterAgent::NewStub(channel)) {}

namespace {

proto::FlowMarker WithDCs(proto::FlowMarker marker, const DCMapper& dc_mapper) {
  auto src_dc = dc_mapper.HostDC(marker.src_addr());
  auto dst_dc = dc_mapper.HostDC(marker.dst_addr());
  if (src_dc != nullptr) {
    marker.set_src_dc(*src_dc);
  }
  if (dst_dc != nullptr) {
    marker.set_dst_dc(*dst_dc);
  }
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
              FlowAggregator* socket_to_host_aggregator,
              FlowStateReporter* flow_state_reporter,
              std::atomic<bool>* should_exit,
              grpc::ClientReaderWriter<proto::InfoBundle, proto::AllocBundle>*
                  io_stream) {
  do {
    // Step 1: refresh stats on all socket-level flows.
    LOG(INFO) << "TODO: implement HIPRI/LOPRI tracking";
    absl::Status report_status = flow_state_reporter->ReportState(
        [](const proto::FlowMarker&) { return false; });
    if (!report_status.ok()) {
      LOG(ERROR) << "failed to report flow state: " << report_status;
      continue;
    }

    // Step 2: collect a bundle of all socket-level flows while annotating them
    //         with src / dst DC.
    proto::InfoBundle bundle;
    bundle.mutable_bundler()->set_host_id(host_id);
    *bundle.mutable_timestamp() = ToProtoTimestamp(absl::Now());
    flow_state_provider->ForEachActiveFlow(
        [&bundle, dc_mapper](absl::Time time, const proto::FlowInfo& info) {
          auto send_info = bundle.add_flow_infos();
          *send_info = info;
          *send_info->mutable_flow() = WithDCs(info.flow(), *dc_mapper);
        });

    // Step 3: aggregate socket-level flows to host-level.
    socket_to_host_aggregator->Update(bundle);

    // Step 4: collect a bundle of all src/dst DC-level flows on the host.
    bundle.clear_flow_infos();
    socket_to_host_aggregator->ForEachAgg(
        [&bundle](absl::Time time, const proto::AggInfo& info) {
          *bundle.add_flow_infos() = info.parent();
        });

    // Step 5: send to cluster agent.
    io_stream->Write(bundle);
  } while (!FlaggedOrWaitFor(inform_period, should_exit));

  io_stream->WritesDone();
}

void EnforceAlloc(const FlowStateProvider* flow_state_provider,
                  FlowStateReporter* flow_state_reporter,
                  HostEnforcerInterface* enforcer,
                  grpc::ClientReaderWriter<proto::InfoBundle,
                                           proto::AllocBundle>* io_stream) {
  while (true) {
    // Step 1: wait for allocation from cluster agent.
    proto::AllocBundle bundle;
    if (!io_stream->Read(&bundle)) {
      break;
    }

    // Step 2: refresh stats on all socket-level flows so that we can better
    //         track usage across QoS switches.
    LOG(INFO) << "TODO: implement HIPRI/LOPRI tracking";
    absl::Status report_status = flow_state_reporter->ReportState(
        [](const proto::FlowMarker&) { return false; });
    if (!report_status.ok()) {
      LOG(ERROR) << "failed to report flow state: " << report_status;
      continue;
    }

    // Step 3: enforce the new allocation.
    enforcer->EnforceAllocs(*flow_state_provider, bundle);
  }
}

}  // namespace

void HostDaemon::Run(std::atomic<bool>* should_exit) {
  CHECK(io_stream_ == nullptr);

  io_stream_ = stub_->RegisterHost(&context_);

  info_thread_ =
      std::thread(SendInfo, config_.inform_period, config_.host_id, dc_mapper_,
                  flow_state_provider_, socket_to_host_aggregator_.get(),
                  flow_state_reporter_, should_exit, io_stream_.get());

  enforcer_thread_ =
      std::thread(EnforceAlloc, flow_state_provider_, flow_state_reporter_,
                  enforcer_, io_stream_.get());
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
