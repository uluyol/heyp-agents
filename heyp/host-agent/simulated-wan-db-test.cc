#include "heyp/host-agent/simulated-wan-db.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/parse-text.h"

namespace heyp {
namespace {

TEST(SimulatedWanDBTest, Basic) {
  SimulatedWanDB db(ParseTextProto<proto::SimulatedWanConfig>(R"(
    dc_pairs {
      src_dc: "chicago"
      dst_dc: "sydney"
      netem { delay_ms: 321 delay_dist: NETEM_PARETO }
    }
    dc_pairs {
      src_dc: "chicago"
      dst_dc: "sanjose"
      netem { delay_ms: 210 delay_dist: NETEM_UNIFORM }
    }
    dc_pairs {
      src_dc: "sydney"
      dst_dc: "chicago"
      netem { delay_ms: 100 }
    }
  )"));

  const proto::NetemConfig* c = db.GetNetem("chicago", "sydney");
  ASSERT_TRUE(c != nullptr);
  EXPECT_EQ(c->delay_ms(), 321);
  EXPECT_EQ(c->delay_dist(), proto::NETEM_PARETO);

  c = db.GetNetem("chicago", "sanjose");
  ASSERT_TRUE(c != nullptr);
  EXPECT_EQ(c->delay_ms(), 210);
  EXPECT_EQ(c->delay_dist(), proto::NETEM_UNIFORM);

  c = db.GetNetem("sydney", "chicago");
  ASSERT_TRUE(c != nullptr);
  EXPECT_EQ(c->delay_ms(), 100);
  EXPECT_EQ(c->delay_dist(), proto::NETEM_NORMAL);
}

}  // namespace
}  // namespace heyp
