#include "heyp/stats/hdrhistogram.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

MATCHER_P2(RecordApproximatelyEq, pct_margin_frac, value_margin_frac, "") {
  const HdrHistogram::Record& lhs = std::get<0>(arg);
  const HdrHistogram::Record& rhs = std::get<1>(arg);
  return ApproximatelyEqual(lhs, rhs, pct_margin_frac, value_margin_frac);
}

MATCHER_P2(Int64Near, want_a, abs_error_a, "") {
  int64_t got = arg;
  int64_t want = want_a;
  int64_t abs_error = abs_error_a;
  return got - abs_error <= want && want <= got + abs_error;
}

TEST(HdrHistogramTest, Basic) {
  HdrHistogram h({});

  h.RecordValue(1);
  h.RecordValue(10001);
  h.RecordValue(10002);
  h.RecordValue(100000);
  h.RecordValue(200000);
  h.RecordValue(210000);

  EXPECT_THAT(h.Min(), Int64Near(1, 0));
  EXPECT_THAT(h.Max(), Int64Near(210000, 210000 / 1000.0));
  EXPECT_THAT(h.ValueAtPercentile(50), Int64Near(10002, 10000 / 1000));

  std::vector<double> percentiles{
      16.7, 33.3, 50, 66.7, 83.3, 100,
  };

  EXPECT_THAT(h.ValuesAtPercentiles(percentiles),
              testing::ElementsAre(
                  Int64Near(1, 0), Int64Near(10000, 10000 / 1e3),
                  Int64Near(10000, 10000 / 1e3), Int64Near(100000, 100000 / 1e3),
                  Int64Near(200000, 200000 / 1e3), Int64Near(210000, 210000 / 1e3)));

  std::vector<HdrHistogram::Record> expected_records{
      {16.7, 1, 1},      {50, 10000, 2},   {66.7, 100000, 1},
      {83.3, 200000, 1}, {100, 210000, 1},
  };

  EXPECT_THAT(h.RecordedValues(),
              testing::Pointwise(RecordApproximatelyEq(0.01, 0.001), expected_records));
}

}  // namespace
}  // namespace heyp
