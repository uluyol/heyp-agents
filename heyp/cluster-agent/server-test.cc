#include "heyp/cluster-agent/server.h"

#include "gmock/gmock.h"
#include "grpcpp/grpcpp.h"
#include "gtest/gtest.h"
#include "heyp/cluster-agent/full-controller.h"
#include "heyp/host-agent/cluster-agent-channel.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/constructors.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/proto/parse-text.h"

namespace heyp {
namespace {

proto::InfoBundle MakeBundle(int id) {
  id = 1000 + id;

  proto::InfoBundle b;
  b.mutable_bundler()->set_host_id(id);
  *b.mutable_timestamp() = ToProtoTimestamp(absl::Now());

  auto fi1 = b.add_flow_infos();
  fi1->mutable_flow()->set_src_dc("A");
  fi1->mutable_flow()->set_dst_dc("B");
  fi1->mutable_flow()->set_job("NA");
  fi1->mutable_flow()->set_host_id(id);
  fi1->set_predicted_demand_bps(110);
  fi1->set_ewma_usage_bps(100);
  fi1->set_cum_usage_bytes(80);
  fi1->set_cum_hipri_usage_bytes(70);
  fi1->set_cum_lopri_usage_bytes(10);

  auto fi2 = b.add_flow_infos();
  fi2->mutable_flow()->set_src_dc("A");
  fi2->mutable_flow()->set_dst_dc("C");
  fi2->mutable_flow()->set_job("NA");
  fi2->mutable_flow()->set_host_id(id);
  fi2->set_predicted_demand_bps(111);
  fi2->set_ewma_usage_bps(101);
  fi2->set_cum_usage_bytes(81);
  fi2->set_cum_hipri_usage_bytes(70);
  fi2->set_cum_lopri_usage_bytes(11);

  return b;
}

struct TestClient {
  TestClient(std::unique_ptr<proto::ClusterAgent::Stub> stub, int id)
      : ch_(std::move(stub)), id_(id), period_(absl::Milliseconds(50)) {
    ch_.Write(MakeBundle(id_));

    inform_th_ = std::thread([this] {
      while (!dying_.load()) {
        absl::SleepFor(period_);
        ch_.Write(MakeBundle(id_));
      }
      ch_.WritesDone();
    });

    enforce_th_ = std::thread([this] {
      while (!dying_.load()) {
        proto::AllocBundle b;
        ch_.Read(&b);
        absl::SleepFor(absl::Milliseconds(100));
      }
    });
  }

  void Die() { dying_.store(true); }

  void Wait() {
    inform_th_.join();
    enforce_th_.join();
  }

  std::atomic<bool> dying_;
  ClusterAgentChannel ch_;
  const int id_;
  const absl::Duration period_;
  std::thread inform_th_;
  std::thread enforce_th_;
};

std::unique_ptr<TestClient> MakeClient(std::shared_ptr<grpc::Channel> ch, int id) {
  return std::make_unique<TestClient>(proto::ClusterAgent::NewStub(ch), id);
}

TEST(ClusterAgentTest, NoCrash) {
  auto logger = MakeLogger("cluster-agent-test");

  ClusterAgentService service(
      std::make_unique<FullClusterController>(
          NewHostToClusterAggregator(
              std::make_unique<BweDemandPredictor>(absl::Milliseconds(500), 1.2, 30),
              absl::Seconds(500)),
          ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                     type: CA_HEYP_SIGCOMM20
                                     enable_burstiness: true
                                     enable_bonus: true
                                     oversub_factor: 1.0
                                     heyp_acceptable_measured_ratio_over_intended_ratio:
                                         1.0
                                   )"),
                                   ParseTextProto<proto::AllocBundle>(R"(
                                     flow_allocs {
                                       flow { src_dc: "A" dst_dc: "B" }
                                       hipri_rate_limit_bps: 50
                                     }
                                     flow_allocs {
                                       flow { src_dc: "A" dst_dc: "C" }
                                       hipri_rate_limit_bps: 70
                                     }
                                   )"),
                                   1.1)
              .value()),
      absl::Milliseconds(80));
  SPDLOG_LOGGER_INFO(&logger, "starting server");
  std::unique_ptr<grpc::Server> server =
      grpc::ServerBuilder().RegisterService(&service).BuildAndStart();

  SPDLOG_LOGGER_INFO(&logger, "starting clients");
  std::array clients{
      MakeClient(server->InProcessChannel({}), 0),
      MakeClient(server->InProcessChannel({}), 1),
      MakeClient(server->InProcessChannel({}), 2),
      MakeClient(server->InProcessChannel({}), 3),
      MakeClient(server->InProcessChannel({}), 3),
  };

  std::atomic<bool> should_exit;
  std::thread exit_th([&should_exit, &logger] {
    absl::SleepFor(absl::Seconds(3));
    SPDLOG_LOGGER_INFO(&logger, "kill compute loop");
    should_exit.store(true);
  });

  SPDLOG_LOGGER_INFO(&logger, "run compute loop");
  service.RunLoop(&should_exit);
  SPDLOG_LOGGER_INFO(&logger, "compute loop exited");
  exit_th.join();

  SPDLOG_LOGGER_INFO(&logger, "kill clients");
  for (auto& c : clients) {
    c->Die();
  }

  SPDLOG_LOGGER_INFO(&logger, "wait for clients");
  for (auto& c : clients) {
    c->Wait();
  }

  SPDLOG_LOGGER_INFO(&logger, "kill server");
  server->Shutdown();
  server->Wait();
}

TEST(ClusterAgentTest, NoCrashManyResp) {
  auto logger = MakeLogger("cluster-agent-test");

  ClusterAgentService service(
      std::make_unique<FullClusterController>(
          NewHostToClusterAggregator(
              std::make_unique<BweDemandPredictor>(absl::Milliseconds(500), 1.2, 30),
              absl::Seconds(500)),
          ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                     type: CA_HEYP_SIGCOMM20
                                     enable_burstiness: true
                                     enable_bonus: true
                                     oversub_factor: 1.0
                                     heyp_acceptable_measured_ratio_over_intended_ratio:
                                         1.0
                                   )"),
                                   ParseTextProto<proto::AllocBundle>(R"(
                                     flow_allocs {
                                       flow { src_dc: "A" dst_dc: "B" }
                                       hipri_rate_limit_bps: 50
                                     }
                                     flow_allocs {
                                       flow { src_dc: "A" dst_dc: "C" }
                                       hipri_rate_limit_bps: 70
                                     }
                                   )"),
                                   1.1)
              .value()),
      absl::ZeroDuration());
  SPDLOG_LOGGER_INFO(&logger, "starting server");
  int selected_port = 0;
  std::unique_ptr<grpc::Server> server =
      grpc::ServerBuilder()
          .AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(),
                            &selected_port)
          .RegisterService(&service)
          .BuildAndStart();
  ASSERT_NE(server, nullptr) << "failed to start server";

  SPDLOG_LOGGER_INFO(&logger, "starting clients");

  std::array clients{
      MakeClient(grpc::CreateChannel(absl::StrCat("127.0.0.1:", selected_port),
                                     grpc::InsecureChannelCredentials()),
                 0),
  };

  std::atomic<bool> should_exit;
  std::thread exit_th([&should_exit, &logger] {
    absl::SleepFor(absl::Seconds(3));
    SPDLOG_LOGGER_INFO(&logger, "kill compute loop");
    should_exit.store(true);
  });

  SPDLOG_LOGGER_INFO(&logger, "run compute loop");
  service.RunLoop(&should_exit);
  SPDLOG_LOGGER_INFO(&logger, "compute loop exited");
  exit_th.join();

  SPDLOG_LOGGER_INFO(&logger, "kill clients");
  for (auto& c : clients) {
    c->Die();
  }

  SPDLOG_LOGGER_INFO(&logger, "wait for clients");
  for (auto& c : clients) {
    c->Wait();
  }

  SPDLOG_LOGGER_INFO(&logger, "kill server");
  server->Shutdown();
  server->Wait();
}

}  // namespace
}  // namespace heyp
