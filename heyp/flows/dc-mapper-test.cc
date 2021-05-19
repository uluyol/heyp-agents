#include "heyp/flows/dc-mapper.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/parse-text.h"

namespace heyp {
namespace {

TEST(StaticDCMapperTest, Basic) {
  StaticDCMapper mapper(ParseTextProto<proto::StaticDCMapperConfig>(R"(
    mapping {
      entries { host_addr: "10.0.0.1" dc: "chicago" }
      entries { host_addr: "10.0.0.2" dc: "minneapolis" }
      entries { host_addr: "10.0.0.3" dc: "chicago" }
      entries { host_addr: "10.0.0.4" dc: "chicago" }
      entries { host_addr: "10.0.0.5" dc: "minneapolis" }
    }
  )"));

  const std::string* dc = mapper.HostDC("10.0.0.1");
  ASSERT_TRUE(dc != nullptr);
  EXPECT_EQ(*dc, "chicago");

  dc = mapper.HostDC("10.0.0.2");
  ASSERT_TRUE(dc != nullptr);
  EXPECT_EQ(*dc, "minneapolis");

  dc = mapper.HostDC("10.0.0.3");
  ASSERT_TRUE(dc != nullptr);
  EXPECT_EQ(*dc, "chicago");

  dc = mapper.HostDC("10.0.0.4");
  ASSERT_TRUE(dc != nullptr);
  EXPECT_EQ(*dc, "chicago");

  dc = mapper.HostDC("10.0.0.5");
  ASSERT_TRUE(dc != nullptr);
  EXPECT_EQ(*dc, "minneapolis");

  const std::vector<std::string>* hosts = mapper.HostsForDC("chicago");
  ASSERT_TRUE(hosts != nullptr);
  EXPECT_THAT(*hosts, testing::UnorderedElementsAre("10.0.0.1", "10.0.0.3", "10.0.0.4"));

  hosts = mapper.HostsForDC("minneapolis");
  ASSERT_TRUE(hosts != nullptr);
  EXPECT_THAT(*hosts, testing::UnorderedElementsAre("10.0.0.2", "10.0.0.5"));

  EXPECT_THAT(mapper.AllDCs(), testing::UnorderedElementsAre("chicago", "minneapolis"));
}

}  // namespace
}  // namespace heyp
