#include <csignal>
#include <iostream>
#include <string>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "grpcpp/grpcpp.h"
#include "heyp/cli/parse.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/cluster-agent/fast-controller.h"
#include "heyp/cluster-agent/full-controller.h"
#include "heyp/cluster-agent/server.h"
#include "heyp/flows/aggregator.h"
#include "heyp/init/init.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/fileio.h"
#include "heyp/proto/ndjson-logger.h"
#include "heyp/threads/set-name.h"

static std::atomic<bool> should_exit_flag{false};

static void InterruptHandler(int signal) {
  if (signal == SIGINT) {
    should_exit_flag.store(true);
  }
}

namespace heyp {
namespace {

absl::Status Run(const proto::ClusterAgentConfig& c, const proto::AllocBundle& allocs,
                 const std::string& alloc_records_file) {
  auto control_period_or =
      ParseAbslDuration(c.server().control_period(), "control period");
  if (!control_period_or.ok()) {
    return control_period_or.status();
  }

  std::unique_ptr<DemandPredictor> agg_demand_predictor;
  absl::Duration demand_time_window;
  absl::Status predictor_status = ParseDemandPredictorConfig(
      c.flow_aggregator().demand_predictor(), &agg_demand_predictor, &demand_time_window);

  if (!predictor_status.ok()) {
    return predictor_status;
  }

  std::unique_ptr<NdjsonLogger> alloc_recorder;
  std::shared_ptr<ClusterController> controller;
  if (c.controller_type() == proto::CC_FULL) {
    if (!alloc_records_file.empty()) {
      auto alloc_recorder_or = CreateNdjsonLogger(alloc_records_file);
      if (!alloc_recorder_or.ok()) {
        return alloc_recorder_or.status();
      }
      alloc_recorder = std::move(*alloc_recorder_or);
    }

    auto cluster_alloc_or = ClusterAllocator::Create(
        c.allocator(), allocs, c.flow_aggregator().demand_predictor().usage_multiplier(),
        alloc_recorder.get());

    if (!cluster_alloc_or.ok()) {
      return cluster_alloc_or.status();
    }
    controller = std::make_shared<FullClusterController>(
        NewHostToClusterAggregator(std::move(agg_demand_predictor), demand_time_window),
        std::move(*cluster_alloc_or));
  } else if (c.controller_type() == proto::CC_FAST) {
    controller = FastClusterController::Create(c.fast_controller_config(), allocs);
  } else {
    return absl::InvalidArgumentError("unknown controller type");
  }

  auto logger = MakeLogger("main");
  std::vector<std::unique_ptr<ClusterAgentService>> services;
  std::vector<std::unique_ptr<grpc::Server>> servers;
  services.reserve(c.server().addresses_size());
  servers.reserve(c.server().addresses_size());
  for (const auto& address : c.server().addresses()) {
    std::vector<std::string> parts = absl::StrSplit(address, ":");
    int id = 0;
    if (!absl::SimpleAtoi(parts[parts.size() - 1], &id)) {
      SPDLOG_LOGGER_INFO(
          &logger, "failed to parse port in {}: service ids may not be useful", address);
    }

    auto service = std::make_unique<ClusterAgentService>(controller, id);
    servers.push_back(grpc::ServerBuilder()
                          .AddListeningPort(address, grpc::InsecureServerCredentials())
                          .RegisterService(service.get())
                          .BuildAndStart());
    services.push_back(std::move(service));
    SPDLOG_LOGGER_INFO(&logger, "Server listening on {}", address);
  }

  SetCurThreadName("ctl-loop");
  RunLoop(controller, *control_period_or, &should_exit_flag, &logger);
  if (alloc_recorder != nullptr) {
    alloc_recorder->Close().IgnoreError();
  }

  for (std::unique_ptr<grpc::Server>& server : servers) {
    server->Shutdown();
    server->Wait();
  }
  return absl::OkStatus();
}

}  // namespace
}  // namespace heyp

ABSL_FLAG(std::string, alloc_logs, "", "path to write allocation debug logs");

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);
  std::signal(SIGINT, InterruptHandler);

  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " config_path.textproto limits_path.textproto\n";
    return 1;
  }

  heyp::proto::ClusterAgentConfig config;
  if (!heyp::ReadTextProtoFromFile(std::string(argv[1]), &config)) {
    std::cerr << "failed to read config file\n";
    return 2;
  }

  heyp::proto::AllocBundle limits;
  if (!heyp::ReadTextProtoFromFile(std::string(argv[2]), &limits)) {
    std::cerr << "failed to read limit file\n";
    return 2;
  }

  absl::Status s = heyp::Run(config, limits, absl::GetFlag(FLAGS_alloc_logs));
  if (!s.ok()) {
    std::cerr << "failed to run: " << s << "\n";
    return 3;
  }
}
