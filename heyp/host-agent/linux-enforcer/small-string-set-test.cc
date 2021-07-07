#include "heyp/host-agent/linux-enforcer/small-string-set.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

TEST(SmallStringSetTest, Simple1) {
  SmallStringSet set1(std::vector<absl::string_view>{"abc", "xyz", "123", "ab"});

  EXPECT_TRUE(set1.contains("abc"));
  EXPECT_TRUE(set1.contains("123"));
  EXPECT_TRUE(set1.contains("xyz"));
  EXPECT_TRUE(set1.contains("ab"));

  EXPECT_FALSE(set1.contains("abcd"));
  EXPECT_FALSE(set1.contains("a"));
  EXPECT_FALSE(set1.contains("3"));
  EXPECT_FALSE(set1.contains("2"));
  EXPECT_FALSE(set1.contains("1"));
  EXPECT_FALSE(set1.contains("x"));
  EXPECT_FALSE(set1.contains(""));
}

TEST(SmallStringSetTest, EmptyStr) {
  SmallStringSet set1(std::vector<absl::string_view>{""});

  EXPECT_TRUE(set1.contains(""));

  EXPECT_FALSE(set1.contains("abcd"));
  EXPECT_FALSE(set1.contains("a"));
  EXPECT_FALSE(set1.contains("3"));
  EXPECT_FALSE(set1.contains("2"));
  EXPECT_FALSE(set1.contains("1"));
  EXPECT_FALSE(set1.contains("x"));
}

TEST(SmallStringSetTest, Empty) {
  SmallStringSet set1(std::vector<absl::string_view>{});

  EXPECT_FALSE(set1.contains(""));
  EXPECT_FALSE(set1.contains("abcd"));
  EXPECT_FALSE(set1.contains("a"));
  EXPECT_FALSE(set1.contains("3"));
  EXPECT_FALSE(set1.contains("2"));
  EXPECT_FALSE(set1.contains("1"));
  EXPECT_FALSE(set1.contains("x"));
}

}  // namespace
}  // namespace heyp
