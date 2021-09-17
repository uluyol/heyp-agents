#include "heyp/host-agent/daemon.h"

#include "absl/cleanup/cleanup.h"
#include "absl/functional/bind_front.h"
#include "absl/time/clock.h"
#include "enforcer.h"
#include "heyp/log/spdlog.h"
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
      fine_grained_flow_state_logger_(-1),
      enforcer_(enforcer),
      channel_(proto::ClusterAgent::NewStub(channel)) {
  if (!config.stats_log_file.empty()) {
    absl::Status st = flow_state_logger_.Init(config.stats_log_file);
    if (!st.ok()) {
      auto logger = MakeLogger("host-daemon");
      SPDLOG_LOGGER_ERROR(&logger, "failed to init flow_state_logger_: {}", st);
    }
  }
  if (!config.fine_grained_stats_log_file.empty()) {
    absl::Status st =
        fine_grained_flow_state_logger_.Init(config.fine_grained_stats_log_file);
    if (!st.ok()) {
      auto logger = MakeLogger("host-daemon");
      SPDLOG_LOGGER_ERROR(&logger, "failed to init fine_grained_flow_state_logger_: {}",
                          st);
    }
  }
}

namespace {

bool WithDCsAndJob(proto::FlowMarker marker, const DCMapper& dc_mapper,
                   const std::string& job_name, proto::FlowMarker* out) {
  auto src_dc = dc_mapper.HostDC(marker.src_addr());
  auto dst_dc = dc_mapper.HostDC(marker.dst_addr());
  if (src_dc != nullptr) {
    marker.set_src_dc(*src_dc);
  }
  if (dst_dc != nullptr) {
    marker.set_dst_dc(*dst_dc);
  }
  marker.set_job(job_name);
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

}  // namespace

class LogTime {
 public:
  LogTime() : t_(absl::Now()) {}

  absl::Time Get() {
    absl::MutexLock l(&mu_);
    return t_;
  }

  void UpdateToNow() {
    auto now = absl::Now();
    absl::MutexLock l(&mu_);
    t_ = now;
  }

 private:
  absl::Mutex mu_;
  absl::Time t_ ABSL_GUARDED_BY(mu_);
};

void HostDaemon::CollectStats(absl::Duration period, bool force_run,
                              NdjsonLogger* flow_state_logger,
                              NdjsonLogger* fine_grained_flow_state_logger,
                              std::atomic<bool>* should_exit,
                              std::shared_ptr<LogTime> last_enforcer_log_time) {
  auto start_time = std::chrono::steady_clock::now();
  auto logger = MakeLogger("collect-stats");

  SPDLOG_LOGGER_INFO(&logger, "begin loop");
  absl::Cleanup loop_done = [flow_state_logger, &logger] {
    if (flow_state_logger->should_log()) {
      absl::Status st = flow_state_logger->Close();
      if (!st.ok()) {
        SPDLOG_LOGGER_WARN(&logger, "error closing flow state logger: {}", st);
      }
    }
    SPDLOG_LOGGER_INFO(&logger, "end loop");
  };

  SPDLOG_LOGGER_INFO(&logger, "will collect stats once every ", period);

  // Wait the first time since Run() refreshes once
  while (force_run || !FlaggedOrWaitFor(period, should_exit)) {
    absl::Duration elapsed =
        absl::FromChrono(std::chrono::steady_clock::now() - start_time);
    SPDLOG_LOGGER_INFO(&logger, "refresh stats {} after start", elapsed);

    force_run = false;
    // Step 1: refresh stats on all socket-level flows.
    IsLopriFunc is_lopri = enforcer_->GetIsLopriFunc();
    absl::Status report_status = flow_state_reporter_->ReportState(is_lopri);
    if (!report_status.ok()) {
      SPDLOG_LOGGER_ERROR(&logger, "failed to report flow state: {}", report_status);
      continue;
    }

    // Step 2: collect a bundle of all socket-level flows while annotating them
    //         with src / dst DC.
    proto::InfoBundle bundle;
    SPDLOG_LOGGER_INFO(&logger, "collect bundle");
    bundle.mutable_bundler()->set_host_id(config_.host_id);
    *bundle.mutable_timestamp() = ToProtoTimestamp(absl::Now());
    flow_state_provider_->ForEachActiveFlow(
        [&bundle, this](absl::Time time, const proto::FlowInfo& info) {
          proto::FlowInfo send_info = info;
          if (WithDCsAndJob(info.flow(), *dc_mapper_, config_.job_name,
                            send_info.mutable_flow())) {
            *bundle.add_flow_infos() = std::move(send_info);
          }
        });

    // Step 2.5: log all fine-grained flows on the host, if requested
    if (fine_grained_flow_state_logger->should_log()) {
      SPDLOG_LOGGER_INFO(&logger, "log fine-grained flow infos to disk");
      absl::Status log_status = fine_grained_flow_state_logger->Write(bundle);
      if (!log_status.ok()) {
        SPDLOG_LOGGER_WARN(&logger, "failed to log fine-grained flow infos: {}",
                           log_status);
      }
    }

    // Step 3: aggregate socket-level flows to host-level.
    SPDLOG_LOGGER_INFO(&logger, "aggregate info to host-level");
    socket_to_host_aggregator_->Update(bundle);

    // Step 3.5: log all src/dst DC-level flows on the host, if requested.
    if (flow_state_logger->should_log()) {
      SPDLOG_LOGGER_INFO(&logger, "log flow infos to disk");
      bundle.clear_flow_infos();
      socket_to_host_aggregator_->ForEachAgg(
          [&bundle](absl::Time time, const proto::AggInfo& info) {
            *bundle.add_flow_infos() = info.parent();
          });

      absl::Status log_status = flow_state_logger->Write(bundle);
      if (!log_status.ok()) {
        SPDLOG_LOGGER_WARN(&logger, "failed to log flow infos: {}", log_status);
      }
    } else {
      SPDLOG_LOGGER_INFO(&logger, "null log: don't log flow infos to disk");
    }

    // Step 4: log enforcer state (if not logged for some time)
    if (last_enforcer_log_time->Get() + absl::Seconds(15) < absl::Now()) {
      enforcer_->LogState();
      last_enforcer_log_time->UpdateToNow();
    }
  }
}

void HostDaemon::SendInfos(std::atomic<bool>* should_exit) {
  auto logger = MakeLogger("send-info");
  SPDLOG_LOGGER_INFO(&logger, "begin loop");
  absl::Cleanup loop_done = [&logger] { SPDLOG_LOGGER_INFO(&logger, "end loop"); };

  do {
    // Step 1: collect a bundle of all src/dst DC-level flows on the host.
    proto::InfoBundle bundle;
    bundle.mutable_bundler()->set_host_id(config_.host_id);
    *bundle.mutable_timestamp() = ToProtoTimestamp(absl::Now());

    socket_to_host_aggregator_->ForEachAgg(
        [&bundle](absl::Time time, const proto::AggInfo& info) {
          *bundle.add_flow_infos() = info.parent();
        });

    // Step 2: send to cluster agent.
    SPDLOG_LOGGER_INFO(&logger, "sending info bundle to cluster agent with {} FGs",
                       bundle.flow_infos_size());
    if (auto st = channel_.Write(bundle); !st.ok()) {
      SPDLOG_LOGGER_WARN(&logger,
                         "failed to send info bundle to cluster agent with {} FGs: {}",
                         bundle.flow_infos_size(), st.error_message());
    }
  } while (!FlaggedOrWaitFor(config_.inform_period, should_exit));

  channel_.WritesDone();
  channel_.TryCancel();
}

void HostDaemon::EnforceAllocs(std::atomic<bool>* should_exit,
                               std::shared_ptr<LogTime> last_enforcer_log_time) {
  auto logger = MakeLogger("enforce-alloc");
  SPDLOG_LOGGER_INFO(&logger, "begin loop");
  absl::Cleanup loop_done = [&logger] { SPDLOG_LOGGER_INFO(&logger, "end loop"); };

  // Init: log enforcer state
  enforcer_->LogState();
  last_enforcer_log_time->UpdateToNow();

  while (!should_exit->load()) {
    // Step 1: wait for allocation from cluster agent.
    proto::AllocBundle bundle;
    if (auto st = channel_.Read(&bundle); !st.ok()) {
      SPDLOG_LOGGER_WARN(&logger, "failed to read alloc bundle: {}", st.error_message());
      continue;
    }

    SPDLOG_LOGGER_INFO(&logger, "got alloc bundle from cluster agent for {} FGs",
                       bundle.flow_allocs_size());
    // Step 2: enforce the new allocation.
    enforcer_->EnforceAllocs(*flow_state_provider_, bundle);

    // Step 3: log enforcer state
    enforcer_->LogState();
    last_enforcer_log_time->UpdateToNow();
  }
}

void HostDaemon::Run(std::atomic<bool>* should_exit) {
  auto last_enforcer_log_time = std::make_shared<LogTime>();

  // Make sure CollectStats has run at least once before we start reporting anything to
  // the cluster agent or start enforcement.
  {
    NdjsonLogger empty_logger(-1);
    std::atomic<bool> once_should_exit = true;
    CollectStats(absl::ZeroDuration(), true, &empty_logger, &empty_logger,
                 &once_should_exit, last_enforcer_log_time);
  }

  collect_stats_thread_ =
      std::thread(&HostDaemon::CollectStats, this, config_.collect_stats_period, false,
                  &flow_state_logger_, &fine_grained_flow_state_logger_, should_exit,
                  last_enforcer_log_time);
  info_thread_ = std::thread(&HostDaemon::SendInfos, this, should_exit);
  enforcer_thread_ =
      std::thread(&HostDaemon::EnforceAllocs, this, should_exit, last_enforcer_log_time);
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
