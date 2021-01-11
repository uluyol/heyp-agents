#include "heyp/host-agent/daemon.h"

#include <atomic>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/parse_text.h"

namespace heyp {
namespace {

MATCHER_P(HostAllocEq, other, "") {
  if (arg.flow_allocs_size() != other.flow_allocs_size()) {
    return false;
  }
  for (int i = 0; i < arg.flow_allocs_size(); i++) {
    const proto::FlowAlloc& a = arg.flow_allocs(i);
    const proto::FlowAlloc& b = other.flow_allocs(i);
    if (!IsSameFlow(a.marker(), b.marker())) {
      return false;
    }
    if (a.hipri_rate_limit_bps() != b.hipri_rate_limit_bps()) {
      return false;
    }
    if (a.lopri_rate_limit_bps() != b.lopri_rate_limit_bps()) {
      return false;
    }
  }
  return true;
}

class TestClusterAgentService final : public proto::ClusterAgent::Service {
 public:
  TestClusterAgentService(std::vector<proto::HostAlloc> responses)
      : responses_(std::move(responses)), num_iters_(0) {}

  grpc::Status RegisterHost(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<proto::HostAlloc, proto::HostInfo>* stream)
      override {
    while (true) {
      proto::HostInfo info;
      if (!stream->Read(&info)) {
        break;
      }

      int64_t i = num_iters_.load();
      proto::HostAlloc alloc;
      if (i < responses_.size()) {
        alloc = responses_[i];
      }
      stream->Write(alloc);
      ++num_iters_;
    }
    return grpc::Status::OK;
  }

  int64_t num_iters() { return num_iters_.load(); }

 private:
  const std::vector<proto::HostAlloc> responses_;
  std::atomic<int64_t> num_iters_;
};

class InProcessTestServer {
 public:
  explicit InProcessTestServer(std::vector<proto::HostAlloc> responses)
      : expected_num_iters_(responses.size()), service_(std::move(responses)) {
    server_ = grpc::ServerBuilder().RegisterService(&service_).BuildAndStart();
  }

  std::shared_ptr<grpc::Channel> GetChannel() {
    return server_->InProcessChannel({});
  }

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
              (absl::FunctionRef<void(const FlowState&)> func),
              (const override));
  MOCK_METHOD(void, ForEachFlow,
              (absl::FunctionRef<void(const FlowState&)> func),
              (const override));
};

class MockFlowStateReporter : public FlowStateReporter {
 public:
  MOCK_METHOD(absl::Status, ReportState, (), (override));
};

class MockHostEnforcer : public HostEnforcerInterface {
 public:
  MOCK_METHOD(void, EnforceAllocs,
              (const FlowStateProvider& flow_state_provider,
               const proto::HostAlloc& host_alloc),
              (override));
};

TEST(HostDaemonTest, CreateAndTeardownNoRun) {
  InProcessTestServer server({});
  MockFlowStateProvider flow_state_provider;
  MockFlowStateReporter flow_state_reporter;
  StaticDCMapper dc_mapper({});
  MockHostEnforcer enforcer;
  EXPECT_CALL(flow_state_provider, ForEachActiveFlow(testing::_)).Times(0);
  EXPECT_CALL(flow_state_reporter, ReportState()).Times(0);
  EXPECT_CALL(enforcer, EnforceAllocs(testing::_, testing::_)).Times(0);
  {
    HostDaemon daemon(server.GetChannel(),
                      {.inform_period = absl::Milliseconds(100)}, &dc_mapper,
                      &flow_state_provider, &flow_state_reporter, &enforcer);
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
  EXPECT_CALL(flow_state_reporter, ReportState()).Times(testing::AtLeast(0));
  EXPECT_CALL(enforcer, EnforceAllocs(testing::_, testing::_))
      .Times(testing::AtLeast(0));
  {
    HostDaemon daemon(server.GetChannel(),
                      {.inform_period = absl::Milliseconds(100)}, &dc_mapper,
                      &flow_state_provider, &flow_state_reporter, &enforcer);
    std::atomic<bool> exit(true);
    daemon.Run(&exit);
  }

  server.Teardown();
}

TEST(HostDaemonTest, CallsIntoHostEnforcer) {
  const std::vector<proto::HostAlloc> allocs{
      ParseTextProto<proto::HostAlloc>(R"(
        flow_allocs: {
          marker: {
            src_dc: "us-east",
            dst_dc: "us-central",
            protocol: TCP,
          }
          hipri_rate_limit_bps: 100,
          lopri_rate_limit_bps: 50,
        }
        flow_allocs: {
          marker: {
            src_dc: "us-east",
            dst_dc: "us-west",
            protocol: TCP,
          }
          hipri_rate_limit_bps: 1000,
          lopri_rate_limit_bps: 200,
        }
      )"),
      ParseTextProto<proto::HostAlloc>(R"(
        flow_allocs: {
          marker: {
            src_dc: "us-east",
            dst_dc: "us-central",
            protocol: TCP,
          }
          hipri_rate_limit_bps: 110,
          lopri_rate_limit_bps: 50,
        }
      )"),
      ParseTextProto<proto::HostAlloc>(R"(
        flow_allocs: {
          marker: {
            src_dc: "us-east",
            dst_dc: "us-central",
            protocol: TCP,
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
  EXPECT_CALL(flow_state_reporter, ReportState()).Times(testing::AtLeast(0));
  {
    testing::InSequence seq;
    for (const auto& entry : allocs) {
      EXPECT_CALL(enforcer, EnforceAllocs(testing::_, HostAllocEq(entry)))
          .Times(1);
    }
    EXPECT_CALL(enforcer, EnforceAllocs(testing::_, testing::_))
        .Times(testing::AtLeast(0));
  }
  {
    HostDaemon daemon(server.GetChannel(),
                      {.inform_period = absl::Milliseconds(10)}, &dc_mapper,
                      &flow_state_provider, &flow_state_reporter, &enforcer);
    std::atomic<bool> exit(false);
    daemon.Run(&exit);
    absl::SleepFor(absl::Milliseconds(150));
    exit.store(true);
  }

  server.Teardown();
}

}  // namespace
}  // namespace heyp
