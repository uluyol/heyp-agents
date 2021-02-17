#include "heyp/host-agent/daemon.h"

#include <atomic>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/flows/aggregator.h"
#include "heyp/proto/parse-text.h"
#include "heyp/proto/testing.h"

namespace heyp {
namespace {

class TestClusterAgentService final : public proto::ClusterAgent::Service {
 public:
  TestClusterAgentService(std::vector<proto::AllocBundle> responses)
      : responses_(std::move(responses)), num_iters_(0) {}

  grpc::Status RegisterHost(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<proto::AllocBundle, proto::InfoBundle>* stream) override {
    while (true) {
      proto::InfoBundle infos;
      if (!stream->Read(&infos)) {
        break;
      }

      int64_t i = num_iters_.load();
      proto::AllocBundle bundle;
      if (i < responses_.size()) {
        bundle = responses_[i];
      }
      stream->Write(bundle);
      ++num_iters_;
    }
    return grpc::Status::OK;
  }

  int64_t num_iters() { return num_iters_.load(); }

 private:
  const std::vector<proto::AllocBundle> responses_;
  std::atomic<int64_t> num_iters_;
};

class InProcessTestServer {
 public:
  explicit InProcessTestServer(std::vector<proto::AllocBundle> responses)
      : expected_num_iters_(responses.size()), service_(std::move(responses)) {
    server_ = grpc::ServerBuilder().RegisterService(&service_).BuildAndStart();
  }

  std::shared_ptr<grpc::Channel> GetChannel() { return server_->InProcessChannel({}); }

  int64_t num_iters() { return service_.num_iters(); }

  void Teardown() {
    server_->Shutdown();
    server_->Wait();

    ASSERT_THAT(service_.num_iters(), testing::Ge(expected_num_iters_));
  }

 private:
  const int64_t expected_num_iters_;
  TestClusterAgentService service_;
  std::unique_ptr<grpc::Server> server_;
};

class MockFlowStateProvider : public FlowStateProvider {
 public:
  MOCK_METHOD(void, ForEachActiveFlow,
              (absl::FunctionRef<void(absl::Time, const proto::FlowInfo&)> func),
              (const override));
  MOCK_METHOD(void, ForEachFlow,
              (absl::FunctionRef<void(absl::Time, const proto::FlowInfo&)> func),
              (const override));
};

class MockFlowStateReporter : public FlowStateReporter {
 public:
  MOCK_METHOD(absl::Status, ReportState,
              (absl::FunctionRef<bool(const proto::FlowMarker&)> is_lopri), (override));
};

class MockHostEnforcer : public HostEnforcerInterface {
 public:
  MOCK_METHOD(void, EnforceAllocs,
              (const FlowStateProvider& flow_state_provider,
               const proto::AllocBundle& bundle),
              (override));
};

std::unique_ptr<FlowAggregator> MakeFlowAggregator() {
  return NewConnToHostAggregator(
      absl::make_unique<BweDemandPredictor>(absl::Seconds(60), 1.2, 0),
      absl::Seconds(60));
}

TEST(HostDaemonTest, CreateAndTeardownNoRun) {
  InProcessTestServer server({});
  MockFlowStateProvider flow_state_provider;
  MockFlowStateReporter flow_state_reporter;
  StaticDCMapper dc_mapper({});
  MockHostEnforcer enforcer;
  EXPECT_CALL(flow_state_provider, ForEachActiveFlow(testing::_)).Times(0);
  EXPECT_CALL(flow_state_reporter, ReportState(testing::_)).Times(0);
  EXPECT_CALL(enforcer, EnforceAllocs(testing::_, testing::_)).Times(0);
  {
    HostDaemon daemon(server.GetChannel(), {.inform_period = absl::Milliseconds(100)},
                      &dc_mapper, &flow_state_provider, MakeFlowAggregator(),
                      &flow_state_reporter, &enforcer);
  }
  server.Teardown();
}

TEST(HostDaemonTest, CreateAndTeardownNoActions) {
  InProcessTestServer server({});
  MockFlowStateProvider flow_state_provider;
  MockFlowStateReporter flow_state_reporter;
  StaticDCMapper dc_mapper({});
  MockHostEnforcer enforcer;
  EXPECT_CALL(flow_state_provider, ForEachActiveFlow(testing::_))
      .Times(testing::AtLeast(0));
  EXPECT_CALL(flow_state_reporter, ReportState(testing::_)).Times(testing::AtLeast(0));
  EXPECT_CALL(enforcer, EnforceAllocs(testing::_, testing::_)).Times(testing::AtLeast(0));
  {
    HostDaemon daemon(server.GetChannel(), {.inform_period = absl::Milliseconds(100)},
                      &dc_mapper, &flow_state_provider, MakeFlowAggregator(),
                      &flow_state_reporter, &enforcer);
    std::atomic<bool> exit(true);
    daemon.Run(&exit);
  }

  server.Teardown();
}

TEST(HostDaemonTest, CallsIntoHostEnforcer) {
  const std::vector<proto::AllocBundle> allocs{
      ParseTextProto<proto::AllocBundle>(R"(
        flow_allocs: {
          flow {
            src_dc: "us-east",
            dst_dc: "us-central",
          }
          hipri_rate_limit_bps: 100,
          lopri_rate_limit_bps: 50,
        }
        flow_allocs: {
          flow {
            src_dc: "us-east",
            dst_dc: "us-west",
          }
          hipri_rate_limit_bps: 1000,
          lopri_rate_limit_bps: 200,
        }
      )"),
      ParseTextProto<proto::AllocBundle>(R"(
        flow_allocs: {
          flow {
            src_dc: "us-east",
            dst_dc: "us-central",
          }
          hipri_rate_limit_bps: 110,
          lopri_rate_limit_bps: 50,
        }
      )"),
      ParseTextProto<proto::AllocBundle>(R"(
        flow_allocs: {
          flow {
            src_dc: "us-east",
            dst_dc: "us-central",
          }
          hipri_rate_limit_bps: 9000,
          lopri_rate_limit_bps: 0,
        }
      )"),
  };

  InProcessTestServer server(allocs);
  MockFlowStateProvider flow_state_provider;
  MockFlowStateReporter flow_state_reporter;
  StaticDCMapper dc_mapper({});
  MockHostEnforcer enforcer;

  EXPECT_CALL(flow_state_provider, ForEachActiveFlow(testing::_))
      .Times(testing::AtLeast(0));
  EXPECT_CALL(flow_state_reporter, ReportState(testing::_)).Times(testing::AtLeast(0));
  {
    testing::InSequence seq;
    for (const auto& entry : allocs) {
      EXPECT_CALL(enforcer, EnforceAllocs(testing::_, AllocBundleEq(entry))).Times(1);
    }
    EXPECT_CALL(enforcer, EnforceAllocs(testing::_, testing::_))
        .Times(testing::AtLeast(0));
  }
  {
    HostDaemon daemon(server.GetChannel(), {.inform_period = absl::Milliseconds(10)},
                      &dc_mapper, &flow_state_provider, MakeFlowAggregator(),
                      &flow_state_reporter, &enforcer);
    std::atomic<bool> exit(false);
    daemon.Run(&exit);
    absl::SleepFor(absl::Milliseconds(150));
    exit.store(true);
  }

  server.Teardown();
}

}  // namespace
}  // namespace heyp
