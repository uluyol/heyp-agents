#include <csignal>

#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/text_format.h"
#include "grpcpp/grpcpp.h"
#include "heyp/cli/parse.h"
#include "heyp/flows/aggregator.h"
#include "heyp/flows/dc-mapper.h"
#include "heyp/host-agent/daemon.h"
#include "heyp/host-agent/enforcer.h"
#include "heyp/host-agent/flow-tracker.h"
#include "heyp/host-agent/linux-enforcer/data.h"
#include "heyp/host-agent/linux-enforcer/enforcer.h"
#include "heyp/init/init.h"
#include "heyp/log/spdlog.h"
#include "heyp/posix/os.h"
#include "heyp/posix/pidfile.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/fileio.h"

static std::atomic<bool> should_exit_flag{false};

static void InterruptHandler(int signal) {
  if (signal == SIGINT) {
    should_exit_flag.store(true);
  }
}

namespace heyp {
namespace {

uint64_t GetUUID() {
  absl::BitGen gen;
  return absl::Uniform<uint64_t>(gen);
}

absl::Status Run(const proto::HostAgentConfig& c) {
  auto logger = MakeLogger("main");

  std::unique_ptr<DemandPredictor> socket_demand_predictor;
  absl::Duration socket_demand_window;
  {
    absl::Status st =
        ParseDemandPredictorConfig(c.flow_tracker().demand_predictor(),
                                   &socket_demand_predictor, &socket_demand_window);
    if (!st.ok()) {
      return st;
    }
  }

  std::unique_ptr<DemandPredictor> host_demand_predictor;
  absl::Duration host_demand_window;
  {
    absl::Status st =
        ParseDemandPredictorConfig(c.socket_to_host_aggregator().demand_predictor(),
                                   &host_demand_predictor, &socket_demand_window);
    if (!st.ok()) {
      return st;
    }
  }

  auto collect_stats_period_or =
      ParseAbslDuration(c.daemon().collect_stats_period(), "collect stats period");
  if (!collect_stats_period_or.ok()) {
    return collect_stats_period_or.status();
  }

  auto inform_period_or =
      ParseAbslDuration(c.daemon().inform_period_dur(), "inform period");
  if (!inform_period_or.ok()) {
    return inform_period_or.status();
  }

  auto cluster_agent_connection_timeout_or = ParseAbslDuration(
      c.daemon().cluster_agent_connection_timeout_dur(), "connection_timeout");
  if (!cluster_agent_connection_timeout_or.ok()) {
    return cluster_agent_connection_timeout_or.status();
  }

  const uint64_t host_id = GetUUID();
  SPDLOG_LOGGER_INFO(&logger, "host assigned id: {}", host_id);

  SPDLOG_LOGGER_INFO(&logger, "creating flow tracker");
  FlowTracker flow_tracker(
      std::move(socket_demand_predictor),
      {
          .usage_history_window = 2 * socket_demand_window,
          .ignore_instantaneous_usage = c.flow_tracker().ignore_instantaneous_usage(),
      });
  SPDLOG_LOGGER_INFO(&logger, "creating flow aggregator");
  std::unique_ptr<FlowAggregator> flow_aggregator =
      NewConnToHostAggregator(std::move(host_demand_predictor), 2 * host_demand_window);
  SPDLOG_LOGGER_INFO(&logger, "creating flow state reporter");
  auto flow_state_reporter_or = SSFlowStateReporter::Create(
      {
          .host_id = host_id,
          .my_addrs = {c.this_host_addrs().begin(), c.this_host_addrs().end()},
          .ss_binary_name = c.flow_state_reporter().ss_binary_name(),
          .collect_aux = !c.daemon().fine_grained_stats_log_file().empty(),
      },
      &flow_tracker);
  if (!flow_state_reporter_or.ok()) {
    return flow_state_reporter_or.status();
  }
  std::unique_ptr<FlowStateReporter> flow_state_reporter =
      std::move(*flow_state_reporter_or);
  SPDLOG_LOGGER_INFO(&logger, "creating dc mapper");
  StaticDCMapper dc_mapper(c.dc_mapper());
  SPDLOG_LOGGER_INFO(&logger, "creating host enforcer");
  std::unique_ptr<HostEnforcer> enforcer;
  if (kHostIsLinux) {
    auto device_or = FindDeviceResponsibleFor(
        {c.this_host_addrs().begin(), c.this_host_addrs().end()}, &logger);
    if (!device_or.ok()) {
      return absl::InternalError(
          absl::StrCat("failed to find device: ", device_or.status().message()));
    }

    auto e = LinuxHostEnforcer::Create(
        device_or.value(), absl::bind_front(&ExpandDestIntoHostsSinglePri, &dc_mapper),
        c.enforcer());
    absl::Status st = e->ResetDeviceConfig();
    if (!st.ok()) {
      SPDLOG_LOGGER_ERROR(&logger, "failed to reset config of device '{}': {}",
                          device_or.value(), st);
    }

    std::string my_dc;
    for (const std::string& addr : c.this_host_addrs()) {
      const std::string* maybe = dc_mapper.HostDC(addr);
      if (maybe != nullptr) {
        my_dc = *maybe;
        break;
      }
    }

    st = e->InitSimulatedWan(
        AllNetemConfigs(dc_mapper, SimulatedWanDB(c.simulated_wan(), dc_mapper), my_dc,
                        host_id),
        /*contains_all_flows=*/true);
    if (!st.ok()) {
      SPDLOG_LOGGER_ERROR(&logger, "failed to init simulated WAN: {}", st);
    }

    enforcer = std::move(e);
  } else {
    SPDLOG_LOGGER_WARN(&logger,
                       "not on Linux: using nop enforcer and no WAN network emulation");
    enforcer = absl::make_unique<NopHostEnforcer>();
  }
  SPDLOG_LOGGER_INFO(&logger, "connecting to cluster agent");
  std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(
      c.daemon().cluster_agent_addr(), grpc::InsecureChannelCredentials());
  bool is_connected = channel->WaitForConnected(
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                   gpr_time_from_micros(
                       absl::ToInt64Microseconds(*cluster_agent_connection_timeout_or),
                       GPR_TIMESPAN)));
  if (!is_connected) {
    return absl::DeadlineExceededError("failed to connect to cluster agent");
  }
  SPDLOG_LOGGER_INFO(&logger, "creating daemon");
  HostDaemon daemon(
      channel,
      {
          .job_name = c.job_name(),
          .host_id = host_id,
          .inform_period = *inform_period_or,
          .collect_stats_period = *collect_stats_period_or,
          .stats_log_file = c.daemon().stats_log_file(),
          .fine_grained_stats_log_file = c.daemon().fine_grained_stats_log_file(),
      },
      &dc_mapper, &flow_tracker, std::move(flow_aggregator), flow_state_reporter.get(),
      enforcer.get());
  SPDLOG_LOGGER_INFO(&logger, "running daemon main loop");
  daemon.Run(&should_exit_flag);
  SPDLOG_LOGGER_INFO(&logger, "exited daemon main loop");
  return absl::OkStatus();
}

}  // namespace
}  // namespace heyp

ABSL_FLAG(std::string, pidfile, "host-agent.pid", "path to write process id");

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);

  std::signal(SIGINT, InterruptHandler);

  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " config_path.textproto\n";
    return 1;
  }

  absl::Status s = heyp::WritePidFile(absl::GetFlag(FLAGS_pidfile));
  if (!s.ok()) {
    std::cerr << "failed to write pid file: " << s << "\n";
    return 2;
  }

  heyp::proto::HostAgentConfig config;
  if (!heyp::ReadTextProtoFromFile(std::string(argv[1]), &config)) {
    std::cerr << "failed to read config file\n";
    return 2;
  }

  s = heyp::Run(config);
  if (!s.ok()) {
    std::cerr << "failed to run: " << s << "\n";
    return 3;
  }
}
