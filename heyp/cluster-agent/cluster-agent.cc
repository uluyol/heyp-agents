#include <csignal>
#include <iostream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "glog/logging.h"
#include "grpcpp/grpcpp.h"
#include "heyp/cli/parse.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/cluster-agent/server.h"
#include "heyp/flows/aggregator.h"
#include "heyp/init/init.h"
#include "heyp/proto/fileio.h"

static std::atomic<bool> should_exit_flag{false};

static void InterruptHandler(int signal) {
  if (signal == SIGINT) {
    should_exit_flag.store(true);
  }
}

namespace heyp {
namespace {

absl::Status Run(const proto::ClusterAgentConfig& c,
                 const proto::AllocBundle& allocs) {
  auto control_period_or =
      ParseAbslDuration(c.server().control_period(), "control period");
  if (!control_period_or.ok()) {
    return control_period_or.status();
  }

  std::unique_ptr<DemandPredictor> agg_demand_predictor;
  absl::Duration demand_time_window;
  absl::Status predictor_status =
      ParseDemandPredictorConfig(c.flow_aggregator().demand_predictor(),
                                 &agg_demand_predictor, &demand_time_window);

  if (!predictor_status.ok()) {
    return predictor_status;
  }

  ClusterAgentService service(
      NewHostToClusterAggregator(std::move(agg_demand_predictor),
                                 demand_time_window),
      ClusterAllocator::Create(c.allocator(), allocs), *control_period_or);

  std::unique_ptr<grpc::Server> server(
      grpc::ServerBuilder()
          .AddListeningPort(c.server().address(),
                            grpc::InsecureServerCredentials())
          .RegisterService(&service)
          .BuildAndStart());
  LOG(INFO) << "Server listening on " << c.server().address();

  while (!should_exit_flag.load()) {
    absl::SleepFor(absl::Seconds(1));
  }

  server->Shutdown();
  server->Wait();
  return absl::OkStatus();
}

}  // namespace
}  // namespace heyp

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);
  std::signal(SIGINT, InterruptHandler);

  if (argc != 3) {
    std::cerr << "usage: " << argv[0]
              << " config_path.textproto limits_path.textproto\n";
    return 1;
  }

  heyp::proto::ClusterAgentConfig config;
  if (!heyp::ReadTextProtoFromFile(std::string(argv[1]), &config)) {
    std::cerr << "failed to read config file\n";
    return 2;
  }

  heyp::proto::AllocBundle limits;
  if (!heyp::ReadTextProtoFromFile(std::string(argv[2]), &config)) {
    std::cerr << "failed to read limit file\n";
    return 2;
  }

  absl::Status s = heyp::Run(config, limits);
  if (!s.ok()) {
    std::cerr << "failed to run: " << s << "\n";
    return 3;
  }
}
