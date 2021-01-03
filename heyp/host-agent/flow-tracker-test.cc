#include "heyp/host-agent/flow-tracker.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/alg/demand-predictor.h"

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

// TODO: Test race: new flow comes and is seen before removal of old flow.
// In this case, we should avoid having a negative usage bps.

}  // namespace
}  // namespace heyp
