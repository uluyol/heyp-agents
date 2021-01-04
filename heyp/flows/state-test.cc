#include "heyp/flows/state.h"

#include "absl/time/clock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

}  // namespace
}  // namespace heyp
