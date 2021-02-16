#include "heyp/host-agent/enforcer-impl/iptables-controller.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace iptables {
namespace {

TEST(ComputeDiffTest, Empty) {
  SettingBatch old_batch;
  SettingBatch new_batch;
  SettingBatch to_add;
  SettingBatch to_del;

  ComputeDiff(old_batch, new_batch, &to_del, &to_add);
  EXPECT_THAT(to_del.settings, testing::IsEmpty());
  EXPECT_THAT(to_add.settings, testing::IsEmpty());

  ComputeDiff(old_batch, new_batch, nullptr, &to_add);
  EXPECT_THAT(to_del.settings, testing::IsEmpty());
  EXPECT_THAT(to_add.settings, testing::IsEmpty());

  ComputeDiff(old_batch, new_batch, &to_del, nullptr);
  EXPECT_THAT(to_del.settings, testing::IsEmpty());
  EXPECT_THAT(to_add.settings, testing::IsEmpty());
}

TEST(ComputeDiffTest, NonEmpty) {
  SettingBatch old_batch{{
      {
          .dst_addr = "127.0.0.1",
          .class_id = "1:45",
          .dscp = "AF3",
      },
      {
          .src_port = 1,
          .dst_addr = "10.0.0.1",
          .class_id = "1:46",
          .dscp = "AF4",
      },
  }};
  SettingBatch new_batch{{
      {
          .dst_addr = "127.0.0.1",
          .class_id = "1:45",
          .dscp = "AF2",
      },
      {
          .dst_port = 1,
          .dst_addr = "10.0.0.1",
          .class_id = "1:46",
          .dscp = "AF4",
      },
      {
          .src_port = 1,
          .dst_addr = "10.0.0.1",
          .class_id = "1:46",
          .dscp = "AF4",
      },
  }};
  SettingBatch to_add;
  SettingBatch to_del;

  ComputeDiff(old_batch, new_batch, &to_del, &to_add);
  EXPECT_THAT(to_del.settings, testing::UnorderedElementsAre(SettingBatch::Setting{
                                   .dst_addr = "127.0.0.1",
                                   .class_id = "1:45",
                                   .dscp = "AF3",
                               }));
  EXPECT_THAT(to_add.settings, testing::UnorderedElementsAre(
                                   SettingBatch::Setting{
                                       .dst_addr = "127.0.0.1",
                                       .class_id = "1:45",
                                       .dscp = "AF2",
                                   },
                                   SettingBatch::Setting{
                                       .dst_port = 1,
                                       .dst_addr = "10.0.0.1",
                                       .class_id = "1:46",
                                       .dscp = "AF4",
                                   }));

  to_add.settings.clear();
  to_del.settings.clear();

  ComputeDiff(old_batch, new_batch, nullptr, &to_add);
  EXPECT_THAT(to_del.settings, testing::IsEmpty());
  EXPECT_THAT(to_add.settings, testing::UnorderedElementsAre(
                                   SettingBatch::Setting{
                                       .dst_addr = "127.0.0.1",
                                       .class_id = "1:45",
                                       .dscp = "AF2",
                                   },
                                   SettingBatch::Setting{
                                       .dst_port = 1,
                                       .dst_addr = "10.0.0.1",
                                       .class_id = "1:46",
                                       .dscp = "AF4",
                                   }));

  to_add.settings.clear();
  to_del.settings.clear();

  ComputeDiff(old_batch, new_batch, &to_del, nullptr);
  EXPECT_THAT(to_add.settings, testing::IsEmpty());
  EXPECT_THAT(to_del.settings, testing::UnorderedElementsAre(SettingBatch::Setting{
                                   .dst_addr = "127.0.0.1",
                                   .class_id = "1:45",
                                   .dscp = "AF3",
                               }));
}

std::string CleanExpectedLines(absl::string_view lines) {
  if (!lines.empty() && lines[0] == '\n') {
    lines = lines.substr(1);
  }
  std::vector<absl::string_view> lines_vec = absl::StrSplit(lines, '\n');
  for (int i = 0; i < lines_vec.size(); ++i) {
    lines_vec[i] = absl::StripLeadingAsciiWhitespace(lines_vec[i]);
  }
  return absl::StrJoin(lines_vec, "\n");
}

TEST(AddRuleLinesToDelete, Basic) {
  absl::Cord lines;
  AddRuleLinesToDelete(
      "eth5",
      SettingBatch{{
          {.dst_addr = "10.0.0.2", .class_id = "1:99", .dscp = "AF41"},
          {.dst_port = 555, .dst_addr = "10.0.0.1", .class_id = "1:100", .dscp = "AF31"},
          {.dst_port = 20, .dst_addr = "10.0.0.1", .class_id = "1:101", .dscp = "AF41"},
          {.src_port = 12,
           .dst_port = 20,
           .dst_addr = "127.0.0.1",
           .class_id = "1:102",
           .dscp = "AF41"},
          {.src_port = 13,
           .dst_port = 20,
           .dst_addr = "127.0.0.1",
           .class_id = "1:103",
           .dscp = "AF31"},
      }},
      lines);

  std::string expected_lines = R"(
    -D OUTPUT -o eth5 -p tcp -m tcp -d 10.0.0.2 -j CLASSIFY --set-class 1:99
    -D OUTPUT -o eth5 -p tcp -m tcp -d 10.0.0.2 -j DSCP --set-dscp-class AF41
    -D OUTPUT -o eth5 -p tcp -m tcp -d 10.0.0.1 --dport 555 -j CLASSIFY --set-class 1:100
    -D OUTPUT -o eth5 -p tcp -m tcp -d 10.0.0.1 --dport 555 -j DSCP --set-dscp-class AF31
    -D OUTPUT -o eth5 -p tcp -m tcp -d 10.0.0.1 --dport 20 -j CLASSIFY --set-class 1:101
    -D OUTPUT -o eth5 -p tcp -m tcp -d 10.0.0.1 --dport 20 -j DSCP --set-dscp-class AF41
    -D OUTPUT -o eth5 -p tcp -m tcp -d 127.0.0.1 --sport 12 --dport 20 -j CLASSIFY --set-class 1:102
    -D OUTPUT -o eth5 -p tcp -m tcp -d 127.0.0.1 --sport 12 --dport 20 -j DSCP --set-dscp-class AF41
    -D OUTPUT -o eth5 -p tcp -m tcp -d 127.0.0.1 --sport 13 --dport 20 -j CLASSIFY --set-class 1:103
    -D OUTPUT -o eth5 -p tcp -m tcp -d 127.0.0.1 --sport 13 --dport 20 -j DSCP --set-dscp-class AF31
  )";

  EXPECT_EQ(lines, CleanExpectedLines(expected_lines));
}

TEST(AddRuleLinesToAdd, Basic) {
  absl::Cord lines;
  AddRuleLinesToAdd(
      "eth5",
      SettingBatch{{
          {.dst_addr = "10.0.0.2", .class_id = "2:99", .dscp = "AF41"},
          {.dst_port = 555, .dst_addr = "10.0.0.1", .class_id = "2:100", .dscp = "AF31"},
          {.dst_port = 20, .dst_addr = "10.0.0.1", .class_id = "2:101", .dscp = "AF41"},
          {.src_port = 12,
           .dst_port = 20,
           .dst_addr = "127.0.0.1",
           .class_id = "2:102",
           .dscp = "AF41"},
          {.src_port = 13,
           .dst_port = 20,
           .dst_addr = "127.0.0.1",
           .class_id = "2:103",
           .dscp = "AF31"},
      }},
      lines);

  std::string expected_lines = R"(
    -A OUTPUT -o eth5 -p tcp -m tcp -d 10.0.0.2 -j CLASSIFY --set-class 2:99
    -A OUTPUT -o eth5 -p tcp -m tcp -d 10.0.0.2 -j DSCP --set-dscp-class AF41
    -A OUTPUT -o eth5 -p tcp -m tcp -d 10.0.0.1 --dport 555 -j CLASSIFY --set-class 2:100
    -A OUTPUT -o eth5 -p tcp -m tcp -d 10.0.0.1 --dport 555 -j DSCP --set-dscp-class AF31
    -A OUTPUT -o eth5 -p tcp -m tcp -d 10.0.0.1 --dport 20 -j CLASSIFY --set-class 2:101
    -A OUTPUT -o eth5 -p tcp -m tcp -d 10.0.0.1 --dport 20 -j DSCP --set-dscp-class AF41
    -A OUTPUT -o eth5 -p tcp -m tcp -d 127.0.0.1 --sport 12 --dport 20 -j CLASSIFY --set-class 2:102
    -A OUTPUT -o eth5 -p tcp -m tcp -d 127.0.0.1 --sport 12 --dport 20 -j DSCP --set-dscp-class AF41
    -A OUTPUT -o eth5 -p tcp -m tcp -d 127.0.0.1 --sport 13 --dport 20 -j CLASSIFY --set-class 2:103
    -A OUTPUT -o eth5 -p tcp -m tcp -d 127.0.0.1 --sport 13 --dport 20 -j DSCP --set-dscp-class AF31
  )";

  EXPECT_EQ(lines, CleanExpectedLines(expected_lines));
}

}  // namespace
}  // namespace iptables
}  // namespace heyp
