#include "heyp/cluster-agent/allocs.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/constructors.h"
#include "heyp/proto/parse-text.h"
#include "heyp/proto/testing.h"

namespace heyp {
namespace {

proto::FlowAlloc ProtoFlowAlloc(FlowMarkerStruct st, int64_t hipri_rate_limit_bps,
                                int64_t lopri_rate_limit_bps) {
  st.protocol = proto::Protocol::UNSET;
  proto::FlowAlloc alloc;
  *alloc.mutable_flow() = ProtoFlowMarker(st);
  alloc.set_hipri_rate_limit_bps(hipri_rate_limit_bps);
  alloc.set_lopri_rate_limit_bps(lopri_rate_limit_bps);
  return alloc;
}

TEST(BundleByHostTest, Empty) {
  EXPECT_THAT(BundleByHost(AllocSet{}), testing::IsEmpty());
}

TEST(BundleByHostTest, MultipleHostsAndFGs) {
  EXPECT_THAT(BundleByHost(AllocSet{
                  {
                      {
                          ProtoFlowAlloc(
                              {
                                  .src_dc = "east-us",
                                  .dst_dc = "west-us",
                                  .host_id = 1,
                              },
                              100, 10),
                          ProtoFlowAlloc(
                              {
                                  .src_dc = "east-us",
                                  .dst_dc = "west-us",
                                  .host_id = 2,
                              },
                              200, 20),
                          ProtoFlowAlloc(
                              {
                                  .src_dc = "east-us",
                                  .dst_dc = "west-us",
                                  .host_id = 5,
                              },
                              500, 50),
                      },
                      {
                          ProtoFlowAlloc(
                              {
                                  .src_dc = "west-us",
                                  .dst_dc = "uk",
                                  .host_id = 3,
                              },
                              900, 90),
                          ProtoFlowAlloc(
                              {
                                  .src_dc = "west-us",
                                  .dst_dc = "uk",
                                  .host_id = 2,
                              },
                              400, 40),
                          ProtoFlowAlloc(
                              {
                                  .src_dc = "west-us",
                                  .dst_dc = "uk",
                                  .host_id = 5,
                              },
                              2500, 250),
                      },
                  },
              }),
              testing::UnorderedElementsAre(
                  testing::Pair(1, AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
              flow_allocs {
                flow {
                  src_dc: "east-us"
                  dst_dc: "west-us"
                  host_id: 1
                }
                hipri_rate_limit_bps: 100
                lopri_rate_limit_bps: 10
              })"))),
                  testing::Pair(2, AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
              flow_allocs {
                flow {
                  src_dc: "east-us"
                  dst_dc: "west-us"
                  host_id: 2
                }
                hipri_rate_limit_bps: 200
                lopri_rate_limit_bps: 20
              }
              flow_allocs {
                flow {
                  src_dc: "west-us"
                  dst_dc: "uk"
                  host_id: 2
                }
                hipri_rate_limit_bps: 400
                lopri_rate_limit_bps: 40
              })"))),
                  testing::Pair(3, AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
              flow_allocs {
                flow {
                  src_dc: "west-us"
                  dst_dc: "uk"
                  host_id: 3
                }
                hipri_rate_limit_bps: 900
                lopri_rate_limit_bps: 90
              })"))),
                  testing::Pair(5, AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
              flow_allocs {
                flow {
                  src_dc: "east-us"
                  dst_dc: "west-us"
                  host_id: 5
                }
                hipri_rate_limit_bps: 500
                lopri_rate_limit_bps: 50
              }
              flow_allocs {
                flow {
                  src_dc: "west-us"
                  dst_dc: "uk"
                  host_id: 5
                }
                hipri_rate_limit_bps: 2500
                lopri_rate_limit_bps: 250
              })")))));
}

}  // namespace
}  // namespace heyp
