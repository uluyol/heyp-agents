#include "heyp/alg/demand-predictor.h"

#include "absl/time/clock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

TEST(BweDemandPredictorTest, Basic) {
  BweDemandPredictor predictor(absl::Seconds(33), 1.2, 3'333'333);

  absl::Time now = absl::Now();

  auto t = [now](absl::Duration d) -> absl::Time { return now + d; };

  EXPECT_EQ(predictor.FromUsage(now, {}), 3'333'333);

  EXPECT_EQ(predictor.FromUsage(now,
                                {
                                    {t(absl::Seconds(-99)), 5'555'555},
                                    {t(absl::Seconds(-95)), 500},
                                    {t(absl::Seconds(-34)), 100'000'000},
                                }),
            3'333'333);

  EXPECT_EQ(predictor.FromUsage(now,
                                {
                                    {t(absl::Seconds(-34)), 5'555'555},
                                    {t(absl::Seconds(-32)), 500},
                                    {t(absl::Seconds(-5)), 1'000'000},
                                }),
            3'333'333);

  EXPECT_EQ(predictor.FromUsage(now,
                                {
                                    {t(absl::Seconds(-34)), 5'555'555},
                                    {t(absl::Seconds(-32)), 4'000'000},
                                    {t(absl::Seconds(-5)), 5'000'000},
                                }),
            6'000'000);

  EXPECT_EQ(predictor.FromUsage(now,
                                {
                                    {t(absl::Seconds(-34)), 5'555'555},
                                    {t(absl::Seconds(-32)), 4'000'000},
                                    {t(absl::Seconds(-5)), 3'500'000},
                                }),
            4'800'000);
}

}  // namespace
}  // namespace heyp
