#include <csignal>
#include <iostream>
#include <string>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "grpcpp/grpcpp.h"
#include "heyp/cli/parse.h"
#include "heyp/cluster-agent/alloc-recorder.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/cluster-agent/server.h"
#include "heyp/flows/aggregator.h"
#include "heyp/init/init.h"
#include "heyp/log/logging.h"
#include "heyp/proto/fileio.h"

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

  std::unique_ptr<AllocRecorder> alloc_recorder;
  if (!alloc_records_file.empty()) {
    auto alloc_recorder_or = AllocRecorder::Create(alloc_records_file);
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

  ClusterAgentService service(
      NewHostToClusterAggregator(std::move(agg_demand_predictor), demand_time_window),
      std::move(*cluster_alloc_or), *control_period_or);

  std::unique_ptr<grpc::Server> server(
      grpc::ServerBuilder()
          .AddListeningPort(c.server().address(), grpc::InsecureServerCredentials())
          .RegisterService(&service)
          .BuildAndStart());
  LOG(INFO) << "Server listening on " << c.server().address();

  service.RunLoop(&should_exit_flag);
  if (alloc_recorder != nullptr) {
    alloc_recorder->Close().IgnoreError();
  }
  server->Shutdown();
  server->Wait();
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
