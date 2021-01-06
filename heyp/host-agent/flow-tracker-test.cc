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

TEST(SSFlowStateReporterTest, BadSSBinary) {
  FlowTracker tracker(
      absl::make_unique<BweDemandPredictor>(absl::Seconds(240), 1.4, 8'000),
      {});

  EXPECT_FALSE(SSFlowStateReporter::Create(
                   {.ss_binary_name = "__DOES_NOT_EXIST_ANYWHERE__"}, &tracker)
                   .ok());
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

}  // namespace
}  // namespace heyp
