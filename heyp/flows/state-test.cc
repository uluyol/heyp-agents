#include "heyp/flows/state.h"

#include "absl/random/random.h"
#include "absl/time/clock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/constructors.h"

namespace heyp {
namespace {

bool HistoryIsSorted(absl::Span<const UsageHistoryEntry> hist) {
  return std::is_sorted(
      hist.begin(), hist.end(),
      [](const UsageHistoryEntry& lhs, const UsageHistoryEntry& rhs) {
        return lhs.time < rhs.time;
      });
}

class MockDemandPredictor : public DemandPredictor {
 public:
  MOCK_METHOD(int64_t, FromUsage,
              (absl::Time now,
               absl::Span<const UsageHistoryEntry> usage_history),
              (const override));
};

TEST(FlowStateTest, GarbageCollectsOldUsage) {
  constexpr absl::Duration window = absl::Seconds(5);
  MockDemandPredictor predictor;

  EXPECT_CALL(
      predictor,
      FromUsage(testing::_, testing::AllOf(testing::SizeIs(testing::Le(11)),
                                           testing::Truly(HistoryIsSorted))))
      .Times(19);

  absl::Time now = absl::Now();
  FlowState state({});
  for (int i = 0; i < 20; i++) {
    state.UpdateUsage(now + absl::Seconds(i), 100, window, &predictor);
  }
}

TEST(FlowStateTest, HistoryAndDemandTracksIncreases) {
  absl::Time now = absl::Now();

  FlowState state(ProtoFlowMarker({
      .src_port = 1234,
      .dst_port = 2345,
  }));

  EXPECT_THAT(state.flow().src_port(), testing::Eq(1234));
  EXPECT_THAT(state.flow().dst_port(), testing::Eq(2345));

  BweDemandPredictor demand_predictor(absl::Seconds(100), 1.1, 5'000);

  int64_t cum_usage_bytes = absl::Uniform(absl::BitGen(), 0, 10'000'000);
  int64_t last_usage_bps = 0;
  for (int i = 0; i < 20; i++) {
    cum_usage_bytes += 100 * i;
    state.UpdateUsage(now + absl::Seconds(i), cum_usage_bytes,
                      absl::Seconds(100), &demand_predictor);
    EXPECT_THAT(state.updated_time(), testing::Eq(now + absl::Seconds(i)));
    EXPECT_THAT(state.cum_usage_bytes(), testing::Eq(cum_usage_bytes));
    if (i >= 2) {
      EXPECT_THAT(state.ewma_usage_bps(), testing::Gt(last_usage_bps));
      EXPECT_THAT(state.predicted_demand_bps(),
                  testing::Gt(1.1 * last_usage_bps));
    }
    if (i >= 1) {
      last_usage_bps = state.ewma_usage_bps();
    }
  }
}

}  // namespace
}  // namespace heyp
