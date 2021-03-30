#include "heyp/host-agent/daemon.h"

#include "absl/functional/bind_front.h"
#include "absl/time/clock.h"
#include "enforcer.h"
#include "glog/logging.h"
#include "heyp/proto/constructors.h"

namespace heyp {

HostDaemon::HostDaemon(const std::shared_ptr<grpc::Channel>& channel, Config config,
                       DCMapper* dc_mapper, FlowStateProvider* flow_state_provider,
                       std::unique_ptr<FlowAggregator> socket_to_host_aggregator,
                       FlowStateReporter* flow_state_reporter, HostEnforcer* enforcer)
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

void CollectStats(absl::Duration period, bool force_run,
                  FlowStateReporter* flow_state_reporter, HostEnforcer* enforcer,
                  std::atomic<bool>* should_exit) {
  // Wait the first time since Run() refreshes once
  while (force_run || !FlaggedOrWaitFor(period, should_exit)) {
    force_run = false;
    // Step 1: refresh stats on all socket-level flows.
    absl::Status report_status = flow_state_reporter->ReportState(
        absl::bind_front(&HostEnforcer::IsLopri, enforcer));
    if (!report_status.ok()) {
      LOG(ERROR) << "failed to report flow state: " << report_status;
      continue;
    }
  }
}

void SendInfo(
    absl::Duration inform_period, uint64_t host_id, const DCMapper* dc_mapper,
    FlowStateProvider* flow_state_provider, FlowAggregator* socket_to_host_aggregator,
    std::atomic<bool>* should_exit,
    grpc::ClientReaderWriter<proto::InfoBundle, proto::AllocBundle>* io_stream) {
  do {
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
    LOG(INFO) << "sending info bundle to cluster agent with " << bundle.flow_infos_size()
              << " FGs";
    io_stream->Write(bundle);
  } while (!FlaggedOrWaitFor(inform_period, should_exit));

  io_stream->WritesDone();
}

void EnforceAlloc(
    const FlowStateProvider* flow_state_provider, HostEnforcer* enforcer,
    grpc::ClientReaderWriter<proto::InfoBundle, proto::AllocBundle>* io_stream) {
  while (true) {
    // Step 1: wait for allocation from cluster agent.
    proto::AllocBundle bundle;
    if (!io_stream->Read(&bundle)) {
      break;
    }

    LOG(INFO) << "got alloc bundle from cluster agent for " << bundle.flow_allocs_size()
              << " FGs";
    // Step 2: enforce the new allocation.
    enforcer->EnforceAllocs(*flow_state_provider, bundle);
  }
}

}  // namespace

void HostDaemon::Run(std::atomic<bool>* should_exit) {
  CHECK(io_stream_ == nullptr);

  // Make sure CollectStats has run at least once before we start reporting anything to
  // the cluster agent or start enforcement.
  {
    std::atomic<bool> once_should_exit = true;
    CollectStats(absl::ZeroDuration(), true, flow_state_reporter_, enforcer_,
                 &once_should_exit);
  }

  io_stream_ = stub_->RegisterHost(&context_);

  collect_stats_thread_ = std::thread(CollectStats, config_.collect_stats_period, false,
                                      flow_state_reporter_, enforcer_, should_exit);

  info_thread_ = std::thread(SendInfo, config_.inform_period, config_.host_id, dc_mapper_,
                             flow_state_provider_, socket_to_host_aggregator_.get(),
                             should_exit, io_stream_.get());

  enforcer_thread_ =
      std::thread(EnforceAlloc, flow_state_provider_, enforcer_, io_stream_.get());
}

HostDaemon::~HostDaemon() {
  if (collect_stats_thread_.joinable()) {
    collect_stats_thread_.join();
  }
  if (info_thread_.joinable()) {
    info_thread_.join();
  }
  if (enforcer_thread_.joinable()) {
    enforcer_thread_.join();
  }
}

}  // namespace heyp
