#include "heyp/proto/alg.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/proto/random.h"

namespace heyp {
namespace {

void testSameFlow(const std::function<bool(const proto::FlowMarker&,
                                           const proto::FlowMarker&)>& eq_func) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    proto::FlowMarker flow;
    FillRandomProto(&flow);

    ASSERT_TRUE(eq_func(flow, flow));

    proto::FlowMarker flow2 = flow;
    bool is_diff = ClearRandomProtoField(&flow2);
    ASSERT_EQ(eq_func(flow, flow2), !is_diff);
    ASSERT_EQ(eq_func(flow2, flow), !is_diff);
  }
}

TEST(AlgTest, IsSameFlow) {
  testSameFlow([](const proto::FlowMarker& lhs, const proto::FlowMarker& rhs) {
    return IsSameFlow(lhs, rhs);
  });
}

TEST(AlgTest, EqFlow) {
  testSameFlow([](const proto::FlowMarker& lhs, const proto::FlowMarker& rhs) {
    return EqFlow()(lhs, rhs);
  });
}

TEST(AlgTest, HashFlow) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    proto::FlowMarker flow;
    FillRandomProto(&flow);

    ASSERT_EQ(HashFlow()(flow), HashFlow()(flow));

    proto::FlowMarker flow2 = flow;
    bool is_diff = ClearRandomProtoField(&flow2);
    if (is_diff) {
      ASSERT_NE(HashFlow()(flow), HashFlow()(flow2));
    } else {
      ASSERT_EQ(HashFlow()(flow), HashFlow()(flow2));
    }
  }
}

}  // namespace
}  // namespace heyp
