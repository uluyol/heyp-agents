#include "heyp/host-agent/flow-tracker.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/proto/constructors.h"

namespace heyp {
namespace {

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

  const auto flow = ProtoFlowMarker({
      .host_unique_id = 0,
  });
}

// TODO: Test race: new flow comes and is seen before removal of old flow.
// In this case, we should avoid having a negative usage bps.

}  // namespace
}  // namespace heyp
