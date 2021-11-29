#include "heyp/cluster-agent/per-agg-allocators/util.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

TEST(ClampFracLOPRITest, EdgeConditionsAndRanges) {
  auto logger = MakeLogger("test");
  EXPECT_EQ(ClampFracLOPRI(&logger, NAN), 0);
  EXPECT_EQ(ClampFracLOPRI(&logger, -0.00001), 0);
  EXPECT_EQ(ClampFracLOPRI(&logger, 0), 0);
  EXPECT_EQ(ClampFracLOPRI(&logger, 0.00001), 0.00001);
  EXPECT_EQ(ClampFracLOPRI(&logger, 0.99999), 0.99999);
  EXPECT_EQ(ClampFracLOPRI(&logger, 1), 1);
  EXPECT_EQ(ClampFracLOPRI(&logger, 1.00001), 1);
  EXPECT_EQ(ClampFracLOPRI(&logger, std::numeric_limits<double>::infinity()), 1);
  EXPECT_EQ(ClampFracLOPRI(&logger, -std::numeric_limits<double>::infinity()), 0);
}

}  // namespace
}  // namespace heyp
