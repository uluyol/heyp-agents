#include "heyp/alg/internal/hash-ring.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace internal {

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

constexpr uint64_t kHashRingMargin = MaxId / 1'000'000;

TEST(HashRingTest, Zero) {
  RingRanges r = HashRing().MatchingRanges();
  EXPECT_GT(r.a.lo, r.a.hi);
  EXPECT_GT(r.b.lo, r.b.hi);
}

TEST(HashRingTest, IsFIFO) {
  HashRing ring;

  ring.Add(0.5);
  RingRanges r = ring.MatchingRanges();
  EXPECT_THAT(r.a, ApproxIdRange(0, MaxId / 2, kHashRingMargin));
  EXPECT_EQ(r.b, IdRange());

  ring.Sub(0.5);
  r = ring.MatchingRanges();
  EXPECT_GT(r.a.lo, r.a.hi);
  EXPECT_GT(r.b.lo, r.b.hi);

  ring.Add(0.4);
  r = ring.MatchingRanges();
  EXPECT_THAT(r.a, ApproxIdRange(MaxId / 2, (MaxId / 10) * 9, kHashRingMargin));
  EXPECT_EQ(r.b, IdRange());

  ring.Add(0.3);
  r = ring.MatchingRanges();
  EXPECT_THAT(r.a, ApproxIdRange(0, MaxId / 5, kHashRingMargin));
  EXPECT_THAT(r.b, ApproxIdRange(MaxId / 2, MaxId, kHashRingMargin));
}

}  // namespace internal
}  // namespace heyp
