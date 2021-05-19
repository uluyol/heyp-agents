#include "heyp/host-agent/linux-enforcer/enforcer.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/parse-text.h"

namespace heyp {
namespace {

TEST(AllNetemConfigs, Basic) {
  std::vector<FlowNetemConfig> configs =
      AllNetemConfigs(StaticDCMapper(ParseTextProto<proto::StaticDCMapperConfig>(R"(
                        mapping {
                          entries { host_addr: "10.0.0.1" dc: "chicago" }
                          entries { host_addr: "10.0.0.2" dc: "sanjose" }
                          entries { host_addr: "10.0.0.3" dc: "sanjose" }
                          entries { host_addr: "10.0.0.4" dc: "newyork" }
                        }
                      )")),
                      SimulatedWanDB(ParseTextProto<proto::SimulatedWanConfig>(R"(
                        dc_pairs {
                          src_dc: "chicago"
                          dst_dc: "sanjose"
                          netem { delay_ms: 53 delay_jitter_ms: 5 }
                        }
                        dc_pairs {
                          src_dc: "chicago"
                          dst_dc: "newyork"
                          netem { delay_ms: 25 delay_jitter_ms: 4 }
                        }
                      )")),
                      "chicago", 1234);

  EXPECT_THAT(configs, testing::UnorderedElementsAre(
                           FlowNetemConfig{
                               .flow = ParseTextProto<proto::FlowMarker>(R"(
                                 src_dc: "chicago"
                                 dst_dc: "sanjose"
                                 host_id: 1234
                               )"),
                               .matched_flows =
                                   std::vector<proto::FlowMarker>{
                                       ParseTextProto<proto::FlowMarker>(R"(
                                         src_dc: "chicago"
                                         dst_dc: "sanjose"
                                         host_id: 1234
                                         dst_addr: "10.0.0.2"
                                       )"),
                                       ParseTextProto<proto::FlowMarker>(R"(
                                         src_dc: "chicago"
                                         dst_dc: "sanjose"
                                         host_id: 1234
                                         dst_addr: "10.0.0.3"
                                       )"),
                                   },
                               .netem = ParseTextProto<proto::NetemConfig>(R"(
                                 delay_ms: 53
                                 delay_jitter_ms: 5
                               )"),
                           },
                           FlowNetemConfig{
                               .flow = ParseTextProto<proto::FlowMarker>(R"(
                                 src_dc: "chicago"
                                 dst_dc: "newyork"
                                 host_id: 1234
                               )"),
                               .matched_flows =
                                   std::vector<proto::FlowMarker>{
                                       ParseTextProto<proto::FlowMarker>(R"(
                                         src_dc: "chicago"
                                         dst_dc: "newyork"
                                         host_id: 1234
                                         dst_addr: "10.0.0.4"
                                       )"),
                                   },
                               .netem = ParseTextProto<proto::NetemConfig>(R"(
                                 delay_ms: 25
                                 delay_jitter_ms: 4
                               )"),
                           }));
}

}  // namespace
}  // namespace heyp
