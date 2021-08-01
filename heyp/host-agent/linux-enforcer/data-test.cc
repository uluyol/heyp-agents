#include "heyp/host-agent/linux-enforcer/data.h"

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/log/spdlog.h"

namespace heyp {
namespace {

constexpr char kCommand[] = "heyp/host-agent/linux-enforcer/fake-ip-addr-for-test";

TEST(FindDeviceResponsibleForTest, Basic) {
  auto logger = MakeLogger("test");

  absl::StatusOr<std::string> dev_or =
      FindDeviceResponsibleFor({"192.168.1.2"}, &logger, kCommand);
  ASSERT_THAT(dev_or.status(), testing::Property(&absl::Status::ok, testing::IsTrue()));
  EXPECT_EQ(dev_or.value(), "eno50");

  dev_or = FindDeviceResponsibleFor({"fe80::9af2:b3ff:fecc:a2b1", "192.168.1.2"}, &logger,
                                    kCommand);
  ASSERT_THAT(dev_or.status(), testing::Property(&absl::Status::ok, testing::IsTrue()));
  EXPECT_EQ(dev_or.value(), "eno50");

  dev_or = FindDeviceResponsibleFor({"128.110.155.6", "fe80::9af2:b3ff:fecc:a2b0"},
                                    &logger, kCommand);
  ASSERT_THAT(dev_or.status(), testing::Property(&absl::Status::ok, testing::IsTrue()));
  EXPECT_EQ(dev_or.value(), "eno49");

  dev_or = FindDeviceResponsibleFor({"128.110.155.6", "192.168.1.2"}, &logger, kCommand);
  ASSERT_THAT(dev_or.status(), testing::Property(&absl::Status::ok, testing::IsFalse()));

  dev_or = FindDeviceResponsibleFor({"127.0.0.1"}, &logger, kCommand);
  ASSERT_THAT(dev_or.status(), testing::Property(&absl::Status::ok, testing::IsTrue()));
  EXPECT_EQ(dev_or.value(), "lo");
}

}  // namespace
}  // namespace heyp
