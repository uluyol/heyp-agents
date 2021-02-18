#include "heyp/host-agent/linux-enforcer/tc-caller.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

TEST(TcCallerTest, YieldsExpectedResult) {
  TcCaller caller("heyp/host-agent/linux-enforcer/fake-tc-for-test");
  EXPECT_THAT(caller.Call({"-j", "qdisc", "list"}),
              testing::Property(&absl::Status::ok, testing::IsTrue()));
  EXPECT_EQ(caller.GetResult()->at(0)["dev"].get_string().value(), "lo");
  EXPECT_EQ(caller.GetResult()->at(1)["dev"].get_string().value(), "ens33");
}

}  // namespace
}  // namespace heyp
