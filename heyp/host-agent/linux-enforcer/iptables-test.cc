#include "heyp/host-agent/linux-enforcer/iptables.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace iptables {

TEST(RunnerTest, SaveInto) {
  std::unique_ptr<Runner> runner = Runner::CreateWithIptablesCommands(
      IpFamily::kIpV4, "", "heyp/host-agent/linux-enforcer/fake-iptables-save-for-test",
      "");

  absl::Cord buffer;
  absl::Status status = runner->SaveInto(Table::kMangle, buffer);
  EXPECT_THAT(status, testing::Property(&absl::Status::ok, testing::IsTrue()));

  absl::Cord expected;
  char buf[1024];
  for (int i = 0; i < 20; ++i) {
    memset(buf, 'A' + i, 1024);
    expected.Append(absl::string_view(buf, 1024));
  }
  EXPECT_EQ(buffer, expected);
}

TEST(RunnerTest, Restore) {
  std::unique_ptr<Runner> runner = Runner::CreateWithIptablesCommands(
      IpFamily::kIpV4, "", "",
      "heyp/host-agent/linux-enforcer/fake-iptables-restore-for-test");

  absl::Cord data;
  char buf[1024];
  for (int i = 0; i < 20; ++i) {
    memset(buf, 'Z' - i, 1024);
    data.Append(buf);
  }

  absl::Status status = runner->Restore(Table::kMangle, data,
                                        {
                                            .flush_tables = false,
                                            .restore_counters = true,
                                        });
  EXPECT_THAT(status, testing::Property(&absl::Status::ok, testing::IsTrue()));
}

}  // namespace iptables
}  // namespace heyp
