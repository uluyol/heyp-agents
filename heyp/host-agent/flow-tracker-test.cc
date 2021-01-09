#include "heyp/host-agent/flow-tracker.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/proto/alg.h"
#include "heyp/proto/constructors.h"

namespace heyp {
namespace {

MATCHER_P(EqFlowNoId, other, "") {
  return IsSameFlow(arg, other, {.cmp_host_unique_id = false});
}

TEST(FlowTrackerTest, RaceNewFlowBeforeOldFlowFinalized) {
  FlowTracker tracker(
      absl::make_unique<BweDemandPredictor>(absl::Seconds(240), 1.4, 8'000),
      {.usage_history_window = absl::Seconds(1000)});

  const proto::FlowMarker flow = ProtoFlowMarker({
      .src_port = 1000,
      .dst_port = 2345,
      .host_unique_id = 0,
  });

  const absl::Time epoch = absl::Now();
  auto time = [epoch](int64_t sec) -> absl::Time {
    return epoch + absl::Seconds(sec);
  };

  tracker.UpdateFlows(time(0), {{flow, 100'000}});
  tracker.UpdateFlows(time(1), {{flow, 200'000}});
  tracker.UpdateFlows(time(2), {{flow, 300'000}});
  tracker.UpdateFlows(time(3), {{flow, 400'000}});

  int times_called = 0;
  tracker.ForEachActiveFlow([&](const FlowState& state) {
    ++times_called;
    ASSERT_THAT(state.flow(), EqFlowNoId(flow));
    ASSERT_THAT(state.flow().host_unique_id(), testing::Eq(1));
    EXPECT_THAT(state.ewma_usage_bps(), testing::Eq(800'000));
  });
  EXPECT_THAT(times_called, testing::Eq(1));

  // RACE: the current flow dies, but before that is registered, a new flow
  // appears with the same src/dst port.

  tracker.UpdateFlows(time(5), {{flow, 50'000}});
  tracker.UpdateFlows(time(6), {{flow, 70'000}});

  times_called = 0;
  tracker.ForEachActiveFlow([&](const FlowState& state) {
    ++times_called;
    ASSERT_THAT(state.flow(), EqFlowNoId(flow));
    ASSERT_THAT(state.flow().host_unique_id(), testing::Eq(2));
    EXPECT_THAT(state.ewma_usage_bps(), testing::Eq(160'000));
  });
  EXPECT_THAT(times_called, testing::Eq(1));

  tracker.FinalizeFlows(time(6), {{flow, 500'000}});

  times_called = 0;
  tracker.ForEachActiveFlow([&](const FlowState& state) { ++times_called; });

  EXPECT_THAT(times_called, testing::Eq(0));
}

TEST(SSFlowStateReporterTest, BadSSBinary) {
  FlowTracker tracker(
      absl::make_unique<BweDemandPredictor>(absl::Seconds(240), 1.4, 8'000),
      {});

  EXPECT_FALSE(SSFlowStateReporter::Create(
                   {.ss_binary_name = "__DOES_NOT_EXIST_ANYWHERE__"}, &tracker)
                   .ok());
}

TEST(SSFlowStateReporterTest, CollectsExpectedOutput) {
  FlowTracker tracker(
      absl::make_unique<BweDemandPredictor>(absl::Seconds(240), 1.4, 8'000),
      {});

  auto reporter_or = SSFlowStateReporter::Create(
      {
          .ss_binary_name = "heyp/host-agent/fake-ss-for-test",
          .host_addr = "140.197.113.99",
      },
      &tracker);

  ASSERT_THAT(reporter_or.status(),
              testing::Property(&absl::Status::ok, testing::IsTrue()));

  std::unique_ptr<SSFlowStateReporter> reporter = std::move(*reporter_or);
  ASSERT_THAT(reporter->ReportState(),
              testing::Property(&absl::Status::ok, testing::IsTrue()));

  constexpr absl::Duration kMaxWaitDur = absl::Seconds(2);
  absl::Time start = absl::Now();
  bool saw_closing = false;
  while (!saw_closing && absl::Now() - start < kMaxWaitDur) {
    absl::flat_hash_map<proto::FlowMarker, std::pair<int64_t, int64_t>,
                        HashHostFlowNoId, EqHostFlowNoId>
        active_usage_bps_cum_bytes;
    tracker.ForEachActiveFlow(
        [&active_usage_bps_cum_bytes](const FlowState& state) {
          active_usage_bps_cum_bytes[state.flow()] = {state.ewma_usage_bps(),
                                                      state.cum_usage_bytes()};
        });

    absl::flat_hash_map<proto::FlowMarker, std::pair<int64_t, int64_t>,
                        HashHostFlowNoId, EqHostFlowNoId>
        dead_usage_bps_cum_bytes;
    tracker.ForEachFlow([&dead_usage_bps_cum_bytes, &active_usage_bps_cum_bytes,
                         &saw_closing](const FlowState& state) {
      if (!active_usage_bps_cum_bytes.contains(state.flow())) {
        dead_usage_bps_cum_bytes[state.flow()] = {state.ewma_usage_bps(),
                                                  state.cum_usage_bytes()};
        saw_closing = true;
      }
    });

    const proto::FlowMarker marker1 = ProtoFlowMarker({
        .src_addr = "140.197.113.99",
        .dst_addr = "165.121.234.111",
        .protocol = proto::TCP,
        .src_port = 22,
        .dst_port = 21364,
    });

    const proto::FlowMarker marker2 = ProtoFlowMarker({
        .src_addr = "140.197.113.99",
        .dst_addr = "165.121.234.111",
        .protocol = proto::TCP,
        .src_port = 99,
        .dst_port = 21364,
    });

    if (active_usage_bps_cum_bytes.size() == 1) {
      ASSERT_THAT(active_usage_bps_cum_bytes,
                  testing::UnorderedElementsAre(testing::Pair(
                      EqFlowNoId(marker2),
                      testing::Pair(testing::Eq(0), testing::Eq(9999)))));
      ASSERT_THAT(dead_usage_bps_cum_bytes,
                  testing::UnorderedElementsAre(testing::Pair(
                      EqFlowNoId(marker1), testing::Pair(testing::Ge(8 * 3900),
                                                         testing::Eq(4140)))));
    } else {
      ASSERT_THAT(
          active_usage_bps_cum_bytes,
          testing::UnorderedElementsAre(
              testing::Pair(EqFlowNoId(marker1),
                            testing::Pair(testing::Eq(0), testing::Eq(240))),
              testing::Pair(EqFlowNoId(marker2),
                            testing::Pair(testing::Eq(0), testing::Eq(9999)))));
      ASSERT_THAT(dead_usage_bps_cum_bytes, testing::IsEmpty());
    }
  }

  EXPECT_TRUE(saw_closing);
}

}  // namespace
}  // namespace heyp
