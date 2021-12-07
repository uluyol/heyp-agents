#include "heyp/alg/downgrade/hash-ring.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

uint64_t Dist(uint64_t a, uint64_t b) {
  uint64_t x = a - b;
  uint64_t y = b - a;
  if (y < x) {
    return y;
  }
  return x;
}

MATCHER_P3(ApproxIdRange, lo, hi, margin, "") {
  bool good_lo = Dist(arg.lo, lo) <= margin;
  bool good_hi = Dist(arg.hi, hi) <= margin;
  return good_lo && good_hi;
}

TEST(IdRangeTest, DefaultRange) {
  IdRange def;
  EXPECT_GT(def.lo, def.hi);
  EXPECT_FALSE(def.Contains(0));
  EXPECT_FALSE(def.Contains(1));
  EXPECT_FALSE(def.Contains(MaxId - 1));
  EXPECT_FALSE(def.Contains(MaxId));
}

TEST(IdRangeTest, ZeroZero) {
  IdRange r{0, 0};
  EXPECT_TRUE(r.Contains(0));
  EXPECT_FALSE(r.Contains(1));
  EXPECT_FALSE(r.Contains(MaxId));
}

TEST(IdRangeTest, ZeroOne) {
  IdRange r{0, 1};
  EXPECT_TRUE(r.Contains(0));
  EXPECT_TRUE(r.Contains(1));
  EXPECT_FALSE(r.Contains(2));
  EXPECT_FALSE(r.Contains(MaxId));
}

TEST(IdRangeTest, Full) {
  IdRange r{0, MaxId};
  EXPECT_TRUE(r.Contains(0));
  EXPECT_TRUE(r.Contains(1));
  EXPECT_TRUE(r.Contains(2));
  EXPECT_TRUE(r.Contains(1'000'000));
  EXPECT_TRUE(r.Contains(MaxId));
}

TEST(RingRanges, Default) {
  RingRanges r;
  EXPECT_EQ(r.a, IdRange());
  EXPECT_EQ(r.b, IdRange());
}

TEST(RingRangesTest, Basic) {
  RingRanges r{
      IdRange{1, 2},
      IdRange{4, 5},
  };

  EXPECT_FALSE(r.Contains(0));
  EXPECT_TRUE(r.Contains(1));
  EXPECT_TRUE(r.Contains(2));
  EXPECT_FALSE(r.Contains(3));
  EXPECT_TRUE(r.Contains(4));
  EXPECT_TRUE(r.Contains(5));
  EXPECT_FALSE(r.Contains(6));
}

const uint64_t kHashRingMargin =
    absl::Uint128Low64((absl::uint128(MaxId) + 1) / 1'000'000);
constexpr absl::uint128 kIdSpaceSize = absl::uint128(MaxId) + 1;

uint64_t IdSpaceSizeDivInto(uint64_t val) {
  return absl::Uint128Low64(kIdSpaceSize / val);
}

TEST(ComputeRangeDiffTest, NoChange) {
  // Have nothing
  RangeDiff expected;
  EXPECT_EQ(HashRing::ComputeRangeDiff(0, 0, 0, 0), expected);
  // Have everything
  EXPECT_EQ(HashRing::ComputeRangeDiff(0, 1, 0, 1), expected);
  // Have everything, offset
  EXPECT_EQ(
      HashRing::ComputeRangeDiff(IdSpaceSizeDivInto(3), 1, IdSpaceSizeDivInto(3), 1),
      expected);
  // Have some, no wrap around
  EXPECT_EQ(
      HashRing::ComputeRangeDiff(IdSpaceSizeDivInto(3), 0.5, IdSpaceSizeDivInto(3), 0.5),
      expected);
  // Have some, with wrap around
  EXPECT_EQ(HashRing::ComputeRangeDiff(IdSpaceSizeDivInto(3) * 2, 0.5,
                                       IdSpaceSizeDivInto(3) * 2, 0.5),
            expected);
}

TEST(ComputeRangeDiffTest, EdgeCasesDel) {
  // Upgrade everything (and wrap around)
  RangeDiff expected{
      .diff = RingRanges{.a = IdRange(0, MaxId)},
      .type = RangeDiffType::kDel,
  };
  EXPECT_EQ(HashRing::ComputeRangeDiff(0, 1, 0, 0), expected);

  // Wrap around but only have upper range
  expected = RangeDiff{
      .diff = RingRanges{.a = IdRange(HashRing::kChunkSize, MaxId)},
      .type = RangeDiffType::kDel,
  };
  EXPECT_EQ(HashRing::ComputeRangeDiff(HashRing::kChunkSize, 1, 0, 0), expected);

  // Wrap around with multiple ranges
  expected = RangeDiff{
      .diff = RingRanges{.a = IdRange(0, IdSpaceSizeDivInto(2) - 1),
                         .b = IdRange(MaxId - HashRing::kChunkSize + 1, MaxId)},
      .type = RangeDiffType::kDel,
  };
  EXPECT_EQ(HashRing::ComputeRangeDiff(MaxId - HashRing::kChunkSize + 1, 0.5,
                                       IdSpaceSizeDivInto(2), 0),
            expected);

  // No wrap around
  expected = RangeDiff{
      .diff =
          RingRanges{.a = IdRange(IdSpaceSizeDivInto(2), IdSpaceSizeDivInto(8) * 5 - 1)},
      .type = RangeDiffType::kDel,
  };
  EXPECT_EQ(HashRing::ComputeRangeDiff(IdSpaceSizeDivInto(2), 0.25,
                                       IdSpaceSizeDivInto(8) * 5, 0.125),
            expected);
}

TEST(ComputeRangeDiffTest, EdgeCasesAdd) {
  // Downgrade everything (and wrap around)
  RangeDiff expected{
      .diff = RingRanges{.a = IdRange(0, MaxId)},
      .type = RangeDiffType::kAdd,
  };
  EXPECT_EQ(HashRing::ComputeRangeDiff(0, 0, 0, 1), expected);

  // Wrap around but only have lower range
  expected = RangeDiff{
      .diff = RingRanges{.a = IdRange(0, IdSpaceSizeDivInto(4) - 1)},
      .type = RangeDiffType::kAdd,
  };
  EXPECT_EQ(
      HashRing::ComputeRangeDiff(IdSpaceSizeDivInto(2), 0.5, IdSpaceSizeDivInto(2), 0.75),
      expected);

  // Wrap around with multiple ranges
  expected = RangeDiff{
      .diff = RingRanges{.a = IdRange(0, IdSpaceSizeDivInto(4) - 1),
                         .b = IdRange(IdSpaceSizeDivInto(8) * 5, MaxId)},
      .type = RangeDiffType::kAdd,
  };
  EXPECT_EQ(HashRing::ComputeRangeDiff(IdSpaceSizeDivInto(2), 0.125,
                                       IdSpaceSizeDivInto(2), 0.75),
            expected);

  // No wrap around
  expected = RangeDiff{
      .diff =
          RingRanges{.a = IdRange(IdSpaceSizeDivInto(2), IdSpaceSizeDivInto(4) * 3 - 1)},
      .type = RangeDiffType::kAdd,
  };
  EXPECT_EQ(
      HashRing::ComputeRangeDiff(IdSpaceSizeDivInto(4), 0.25, IdSpaceSizeDivInto(4), 0.5),
      expected);
}

TEST(FracToRingTest, EdgeCases) {
  EXPECT_EQ(HashRing::FracToRing(0), 0);
  EXPECT_EQ(HashRing::FracToRing(1.0), kIdSpaceSize);
}

TEST(FracToRingTest, Approx) {
  EXPECT_EQ(HashRing::FracToRing(0.25), IdSpaceSizeDivInto(4));
  EXPECT_THAT(HashRing::FracToRing(0.10),
              testing::AllOf(testing::Gt(IdSpaceSizeDivInto(10) - kHashRingMargin),
                             testing::Lt(IdSpaceSizeDivInto(10) + kHashRingMargin)));
}

TEST(HashRingTest, Full) {
  HashRing ring;
  ring.Add(1);
  RingRanges r = ring.MatchingRanges();
  EXPECT_EQ(r.a, IdRange(0, MaxId));
  EXPECT_EQ(r.b, IdRange());
}

TEST(HashRingTest, Zero) {
  RingRanges r = HashRing().MatchingRanges();
  EXPECT_GT(r.a.lo, r.a.hi);
  EXPECT_GT(r.b.lo, r.b.hi);
}

TEST(HashRingTest, IsFIFO) {
  HashRing ring;

  ring.Add(0.5);
  RingRanges r = ring.MatchingRanges();
  EXPECT_THAT(r.a, ApproxIdRange(0, IdSpaceSizeDivInto(2), kHashRingMargin));
  EXPECT_EQ(r.b, IdRange());

  ring.Sub(0.5);
  r = ring.MatchingRanges();
  EXPECT_GT(r.a.lo, r.a.hi);
  EXPECT_GT(r.b.lo, r.b.hi);

  ring.Add(0.4);
  r = ring.MatchingRanges();
  EXPECT_THAT(r.a, ApproxIdRange(IdSpaceSizeDivInto(2), IdSpaceSizeDivInto(10) * 9,
                                 kHashRingMargin));
  EXPECT_EQ(r.b, IdRange());

  ring.Add(0.3);
  r = ring.MatchingRanges();
  EXPECT_THAT(r.a, ApproxIdRange(0, IdSpaceSizeDivInto(5), kHashRingMargin));
  EXPECT_THAT(r.b, ApproxIdRange(IdSpaceSizeDivInto(2), MaxId, kHashRingMargin));
}

TEST(HashRingTest, NoOverlapWhenDrainAndAdd) {
  HashRing ring;
  ring.Add(0.5);
  RingRanges init = ring.MatchingRanges();
  ring.Sub(0.5);
  RingRanges drained = ring.MatchingRanges();
  ring.Add(0.5);
  RingRanges final = ring.MatchingRanges();

  EXPECT_EQ(init.a, IdRange(0, IdSpaceSizeDivInto(2) - 1));
  EXPECT_TRUE(init.b.Empty());

  EXPECT_TRUE(drained.a.Empty());
  EXPECT_TRUE(drained.b.Empty());

  EXPECT_EQ(final.a, IdRange(IdSpaceSizeDivInto(2), MaxId));
  EXPECT_TRUE(final.b.Empty());
  EXPECT_LT(init.a.hi, final.a.lo);
}

}  // namespace
}  // namespace heyp
