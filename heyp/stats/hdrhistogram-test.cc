#include "heyp/stats/hdrhistogram.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/parse-text.h"

namespace heyp {
namespace {

MATCHER_P(BucketApproximatelyEq, value_margin_frac, "") {
  const proto::HdrHistogram::Bucket& lhs = std::get<0>(arg);
  const proto::HdrHistogram::Bucket& rhs = std::get<1>(arg);
  return ApproximatelyEqual(lhs, rhs, value_margin_frac);
}

MATCHER_P2(Int64Near, want_a, abs_error_a, "") {
  int64_t got = arg;
  int64_t want = want_a;
  int64_t abs_error = abs_error_a;
  return got - abs_error <= want && want <= got + abs_error;
}

TEST(HdrHistogramTest, Basic) {
  HdrHistogram h;

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

  auto expected_hist = ParseTextProto<proto::HdrHistogram>(
      R"(
        config {
          lowest_discernible_value: 1
          highest_trackable_value: 30000000000
          significant_figures: 3
        }
        buckets { v: 1 c: 1 }
        buckets { v: 10000 c: 2 }
        buckets { v: 100000 c: 1 }
        buckets { v: 200000 c: 1 }
        buckets { v: 210000 c: 1 }
      )");

  proto::HdrHistogram proto_hist = h.ToProto();

  EXPECT_EQ(proto_hist.config().lowest_discernible_value(),
            expected_hist.config().lowest_discernible_value());
  EXPECT_EQ(proto_hist.config().highest_trackable_value(),
            expected_hist.config().highest_trackable_value());
  EXPECT_EQ(proto_hist.config().significant_figures(),
            expected_hist.config().significant_figures());

  EXPECT_THAT(proto_hist.buckets(),
              testing::Pointwise(BucketApproximatelyEq(0.001), expected_hist.buckets()));

  // round trip

  proto::HdrHistogram roundtripped = HdrHistogram::FromProto(proto_hist).ToProto();

  EXPECT_EQ(roundtripped.config().lowest_discernible_value(),
            expected_hist.config().lowest_discernible_value());
  EXPECT_EQ(roundtripped.config().highest_trackable_value(),
            expected_hist.config().highest_trackable_value());
  EXPECT_EQ(roundtripped.config().significant_figures(),
            expected_hist.config().significant_figures());

  EXPECT_THAT(roundtripped.buckets(),
              testing::Pointwise(BucketApproximatelyEq(0), proto_hist.buckets()));
}

}  // namespace
}  // namespace heyp
