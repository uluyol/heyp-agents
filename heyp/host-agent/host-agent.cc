#include <csignal>

#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "grpcpp/grpcpp.h"
#include "heyp/flows/dc-mapper.h"
#include "heyp/host-agent/daemon.h"
#include "heyp/host-agent/enforcer.h"
#include "heyp/host-agent/flow-tracker.h"
#include "heyp/init/init.h"
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

absl::StatusOr<absl::Duration> ParseAbslDuration(absl::string_view dur,
                                                 absl::string_view field_name) {
  absl::Duration d;
  if (!absl::ParseDuration(dur, &d)) {
    return absl::InvalidArgumentError(
        absl::StrCat("invalid ", field_name, ": ", dur));
  }
  return d;
}

absl::Status Run(const proto::HostAgentConfig& c) {
  auto time_window_or = ParseAbslDuration(
      c.flow_tracker().demand_predictor().time_window_dur(), "time window");
  if (!time_window_or.ok()) {
    return time_window_or.status();
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
  LOG(INFO) << "host assigned id: " << host_id;

  LOG(INFO) << "creating flow tracker";
  FlowTracker flow_tracker(
      absl::make_unique<BweDemandPredictor>(
          *time_window_or,
          c.flow_tracker().demand_predictor().usage_multiplier(),
          c.flow_tracker().demand_predictor().min_demand_bps()),
      {.usage_history_window = 2 * *time_window_or});
  LOG(INFO) << "creating flow state reporter";
  auto flow_state_reporter_or = SSFlowStateReporter::Create(
      {
          .host_id = host_id,
          .my_addrs = {c.flow_state_reporter().this_host_addrs().begin(),
                       c.flow_state_reporter().this_host_addrs().end()},
          .ss_binary_name = c.flow_state_reporter().ss_binary_name(),
      },
      &flow_tracker);
  if (!flow_state_reporter_or.ok()) {
    return flow_state_reporter_or.status();
  }
  std::unique_ptr<FlowStateReporter> flow_state_reporter =
      std::move(*flow_state_reporter_or);
  LOG(INFO) << "creating dc mapper";
  StaticDCMapper dc_mapper(c.dc_mapper());
  LOG(INFO) << "creating host enforcer";
  HostEnforcer enforcer;
  LOG(INFO) << "connecting to cluster agent";
  std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(
      c.daemon().cluster_agent_addr(), grpc::InsecureChannelCredentials());
  bool is_connected = channel->WaitForConnected(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_micros(
          absl::ToInt64Microseconds(*cluster_agent_connection_timeout_or),
          GPR_TIMESPAN)));
  if (!is_connected) {
    return absl::DeadlineExceededError("failed to connect to cluster agent");
  }
  LOG(INFO) << "creating daemon";
  HostDaemon daemon(channel,
                    {
                        .host_id = host_id,
                        .inform_period = *inform_period_or,
                    },
                    &dc_mapper, &flow_tracker, flow_state_reporter.get(),
                    &enforcer);
  LOG(INFO) << "running daemon main loop";
  daemon.Run(&should_exit_flag);
  return absl::OkStatus();
}

}  // namespace
}  // namespace heyp

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);

  std::signal(SIGINT, InterruptHandler);

  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " config_path.textproto\n";
    return 1;
  }

  heyp::proto::HostAgentConfig config;
  if (!heyp::ReadTextProtoFromFile(std::string(argv[1]), &config)) {
    std::cerr << "failed to read config file\n";
    return 2;
  }

  absl::Status s = heyp::Run(config);
  if (!s.ok()) {
    std::cerr << "failed to run: " << s << "\n";
    return 3;
  }
}
