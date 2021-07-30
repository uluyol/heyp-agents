#include "heyp/host-agent/daemon.h"

#include "absl/cleanup/cleanup.h"
#include "absl/functional/bind_front.h"
#include "absl/time/clock.h"
#include "enforcer.h"
#include "heyp/log/logging.h"
#include "heyp/proto/constructors.h"
#include "heyp/proto/heyp.pb.h"

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
      flow_state_logger_(-1),
      enforcer_(enforcer),
      channel_(proto::ClusterAgent::NewStub(channel)) {
  if (!config.stats_log_file.empty()) {
    absl::Status st = flow_state_logger_.Init(config.stats_log_file);
    if (!st.ok()) {
      LOG(ERROR) << "HostDaemon: failed to init flow_state_logger_: " << st;
    }
  }
}

namespace {

bool WithDCs(proto::FlowMarker marker, const DCMapper& dc_mapper,
             proto::FlowMarker* out) {
  auto src_dc = dc_mapper.HostDC(marker.src_addr());
  auto dst_dc = dc_mapper.HostDC(marker.dst_addr());
  if (src_dc != nullptr) {
    marker.set_src_dc(*src_dc);
  }
  if (dst_dc != nullptr) {
    marker.set_dst_dc(*dst_dc);
  }
  *out = std::move(marker);
  return src_dc != nullptr && dst_dc != nullptr;
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
                  FlowStateReporter* flow_state_reporter, const DCMapper* dc_mapper,
                  uint64_t host_id, FlowStateProvider* flow_state_provider,
                  FlowAggregator* socket_to_host_aggregator,
                  NdjsonLogger* flow_state_logger, HostEnforcer* enforcer,
                  std::atomic<bool>* should_exit) {
  auto start_time = std::chrono::steady_clock::now();

  LOG(INFO) << "CollectStats: begin";
  absl::Cleanup loop_done = [flow_state_logger] {
    if (flow_state_logger->should_log()) {
      absl::Status st = flow_state_logger->Close();
      if (!st.ok()) {
        LOG(WARNING) << "error closing flow state logger: " << st;
      }
    }
    LOG(INFO) << "CollectStats: end";
  };

  LOG(INFO) << "will collect stats once every " << period;

  // Wait the first time since Run() refreshes once
  while (force_run || !FlaggedOrWaitFor(period, should_exit)) {
    absl::Duration elapsed =
        absl::FromChrono(std::chrono::steady_clock::now() - start_time);
    LOG(INFO) << "refresh stats " << elapsed << " after start";

    force_run = false;
    // Step 1: refresh stats on all socket-level flows.
    absl::Status report_status = flow_state_reporter->ReportState(
        absl::bind_front(&HostEnforcer::IsLopri, enforcer));
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
          proto::FlowInfo send_info = info;
          if (WithDCs(info.flow(), *dc_mapper, send_info.mutable_flow())) {
            *bundle.add_flow_infos() = std::move(send_info);
          }
        });

    // Step 3: aggregate socket-level flows to host-level.
    socket_to_host_aggregator->Update(bundle);

    // Step 3.5: log all src/dst DC-level flows on the flows, if requested.
    if (flow_state_logger->should_log()) {
      LOG(INFO) << "log flow infos to disk";
      bundle.clear_flow_infos();
      socket_to_host_aggregator->ForEachAgg(
          [&bundle](absl::Time time, const proto::AggInfo& info) {
            *bundle.add_flow_infos() = info.parent();
          });

      absl::Status log_status = flow_state_logger->Write(bundle);
      if (!log_status.ok()) {
        LOG(WARNING) << "failed to log flow infos: " << log_status;
      }
    } else {
      LOG(INFO) << "null log: don't log flow infos to disk";
    }
  }
}

void SendInfo(absl::Duration inform_period, uint64_t host_id,
              FlowAggregator* socket_to_host_aggregator, std::atomic<bool>* should_exit,
              ClusterAgentChannel* channel) {
  LOG(INFO) << "SendInfo: begin";
  absl::Cleanup loop_done = [] { LOG(INFO) << "SendInfo: end"; };

  do {
    // Step 4: collect a bundle of all src/dst DC-level flows on the host.
    proto::InfoBundle bundle;
    bundle.mutable_bundler()->set_host_id(host_id);
    *bundle.mutable_timestamp() = ToProtoTimestamp(absl::Now());

    socket_to_host_aggregator->ForEachAgg(
        [&bundle](absl::Time time, const proto::AggInfo& info) {
          *bundle.add_flow_infos() = info.parent();
        });

    // Step 5: send to cluster agent.
    LOG(INFO) << "sending info bundle to cluster agent with " << bundle.flow_infos_size()
              << " FGs";
    if (auto st = channel->Write(bundle); !st.ok()) {
      LOG(WARNING) << "failed to send info bundle to cluster agent with "
                   << bundle.flow_infos_size() << " FGs: " << st.error_message();
    }
  } while (!FlaggedOrWaitFor(inform_period, should_exit));

  channel->WritesDone();
  channel->TryCancel();
}

void EnforceAlloc(const FlowStateProvider* flow_state_provider, HostEnforcer* enforcer,
                  std::atomic<bool>* should_exit, ClusterAgentChannel* channel) {
  LOG(INFO) << "EnforceAlloc: begin";
  absl::Cleanup loop_done = [] { LOG(INFO) << "EnforceAlloc: end"; };

  while (!should_exit->load()) {
    // Step 1: wait for allocation from cluster agent.
    proto::AllocBundle bundle;
    if (auto st = channel->Read(&bundle); !st.ok()) {
      LOG(WARNING) << "failed to read alloc bundle: " << st.error_message();
      continue;
    }

    LOG(INFO) << "got alloc bundle from cluster agent for " << bundle.flow_allocs_size()
              << " FGs";
    // Step 2: enforce the new allocation.
    enforcer->EnforceAllocs(*flow_state_provider, bundle);
  }
}

}  // namespace

void HostDaemon::Run(std::atomic<bool>* should_exit) {
  // Make sure CollectStats has run at least once before we start reporting anything to
  // the cluster agent or start enforcement.
  {
    NdjsonLogger empty_logger(-1);
    std::atomic<bool> once_should_exit = true;
    CollectStats(absl::ZeroDuration(), true, flow_state_reporter_, dc_mapper_,
                 config_.host_id, flow_state_provider_, socket_to_host_aggregator_.get(),
                 &empty_logger, enforcer_, &once_should_exit);
  }

  collect_stats_thread_ = std::thread(
      CollectStats, config_.collect_stats_period, false, flow_state_reporter_, dc_mapper_,
      config_.host_id, flow_state_provider_, socket_to_host_aggregator_.get(),
      &flow_state_logger_, enforcer_, should_exit);

  info_thread_ = std::thread(SendInfo, config_.inform_period, config_.host_id,
                             socket_to_host_aggregator_.get(), should_exit, &channel_);

  enforcer_thread_ =
      std::thread(EnforceAlloc, flow_state_provider_, enforcer_, should_exit, &channel_);
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
