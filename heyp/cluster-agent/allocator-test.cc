#include "heyp/cluster-agent/allocator.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/alg/debug.h"
#include "heyp/proto/parse-text.h"
#include "heyp/proto/testing.h"

namespace heyp {
namespace {

absl::Time T(int64_t sec) { return absl::UnixEpoch() + absl::Seconds(sec); }

proto::AllocBundle Bundle(const AllocSet& alloc_set) {
  size_t size = 0;
  for (const auto& p : alloc_set.partial_sets) {
    size += p.size();
  }
  proto::AllocBundle b;
  b.mutable_flow_allocs()->Reserve(size);
  for (const auto& p : alloc_set.partial_sets) {
    for (const auto& alloc : p) {
      *b.add_flow_allocs() = alloc;
    }
  }
  return b;
}

TEST(BweClusterAllocatorTest, MissingAllocInConfig) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_BWE
                                          enable_burstiness: false
                                          enable_bonus: true
                                          oversub_factor: 1.0
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 666666
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent { flow { src_dc: "east-us" dst_dc: "central-us" } }
                             children {
                               flow { src_dc: "east-us" dst_dc: "central-us" host_id: 1 }
                               predicted_demand_bps: 600000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "central-us" host_id: 2 }
                               predicted_demand_bps: 60000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "central-us" host_id: 3 }
                               predicted_demand_bps: 6000
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()), AllocBundleEq(proto::AllocBundle()));
}

TEST(BweClusterAllocatorTest, Basic) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_BWE
                                          enable_burstiness: false
                                          enable_bonus: true
                                          oversub_factor: 1.0
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 666666
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent { flow { src_dc: "east-us" dst_dc: "west-us" } }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 600000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 60000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 6000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 4 }
                               predicted_demand_bps: 600
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 5 }
                               predicted_demand_bps: 67
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 599999
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 599999
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  hipri_rate_limit_bps: 599999
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 4 }
                  hipri_rate_limit_bps: 599999
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 5 }
                  hipri_rate_limit_bps: 599999
                })")));
}

TEST(BweClusterAllocatorTest, WithOversub) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_BWE
                                          enable_burstiness: false
                                          enable_bonus: true
                                          oversub_factor: 1.5
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 600
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent { flow { src_dc: "east-us" dst_dc: "west-us" } }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 400
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 200
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 200
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 300
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 300
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  hipri_rate_limit_bps: 300
                })")));
}

TEST(BweClusterAllocatorTest, WithBonus) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_BWE
                                          enable_burstiness: false
                                          enable_bonus: true
                                          oversub_factor: 1.0
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 600
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent { flow { src_dc: "east-us" dst_dc: "west-us" } }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 100
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 200
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 150
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 250
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 250
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  hipri_rate_limit_bps: 250
                })")));
}

TEST(BweClusterAllocatorTest, WithBurstiness) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_BWE
                                          enable_burstiness: true
                                          enable_bonus: true
                                          oversub_factor: 1.0
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 600
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 600
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 400
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 300
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 200
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 400
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 400
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  hipri_rate_limit_bps: 400
                })")));
}

TEST(BweClusterAllocatorTest, WithBurstinessAndCongestion) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_BWE
                                          enable_burstiness: true
                                          enable_bonus: true
                                          oversub_factor: 1.0
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 600
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 800
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 400
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 300
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 200
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 237
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 237
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  hipri_rate_limit_bps: 237
                })")));
}

TEST(BweClusterAllocatorTest, ZeroDemand) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_BWE
                                          enable_burstiness: true
                                          enable_bonus: true
                                          oversub_factor: 1.1
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 600
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 50
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 0
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 0
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 330
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 330
                })")));
}

TEST(BweClusterAllocatorTest, ZeroLimit) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_BWE
                                          enable_burstiness: true
                                          enable_bonus: true
                                          oversub_factor: 1.1
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 0
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 50
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 50
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 20
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 0
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 0
                })")));
}

TEST(HeypSigcomm20ClusterAllocatorTest, MissingAllocInConfig) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: CA_HEYP_SIGCOMM20
                                 enable_burstiness: false
                                 enable_bonus: true
                                 oversub_factor: 1.0
                                 heyp_acceptable_measured_ratio_over_intended_ratio: 0.9
                                 heyp_probe_lopri_when_ambiguous: false
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow { src_dc: "east-us" dst_dc: "west-us" }
                                   hipri_rate_limit_bps: 666666
                                   lopri_rate_limit_bps: 333333
                                 }
                               )"),
                               1)
          .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent { flow { src_dc: "east-us" dst_dc: "central-us" } }
                             children {
                               flow { src_dc: "east-us" dst_dc: "central-us" host_id: 1 }
                               predicted_demand_bps: 600000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "central-us" host_id: 2 }
                               predicted_demand_bps: 60000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "central-us" host_id: 3 }
                               predicted_demand_bps: 6000
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()), AllocBundleEq(proto::AllocBundle()));
}

TEST(HeypSigcomm20ClusterAllocatorTest, Basic) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: CA_HEYP_SIGCOMM20
                                 enable_burstiness: false
                                 enable_bonus: true
                                 oversub_factor: 1.0
                                 heyp_acceptable_measured_ratio_over_intended_ratio: 0.9
                                 heyp_probe_lopri_when_ambiguous: false
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow { src_dc: "east-us" dst_dc: "west-us" }
                                   hipri_rate_limit_bps: 600000
                                   lopri_rate_limit_bps: 333333
                                 }
                               )"),
                               1)
          .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 933300
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 600000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 300000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 30000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 4 }
                               predicted_demand_bps: 3000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 5 }
                               predicted_demand_bps: 300
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 600000
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  lopri_rate_limit_bps: 300008
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  lopri_rate_limit_bps: 300008
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 4 }
                  lopri_rate_limit_bps: 300008
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 5 }
                  lopri_rate_limit_bps: 300008
                })")));
}

TEST(HeypSigcomm20ClusterAllocatorTest, WithOversub) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: CA_HEYP_SIGCOMM20
                                 enable_burstiness: false
                                 enable_bonus: true
                                 oversub_factor: 1.5
                                 heyp_acceptable_measured_ratio_over_intended_ratio: 0.9
                                 heyp_probe_lopri_when_ambiguous: false
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow { src_dc: "east-us" dst_dc: "west-us" }
                                   hipri_rate_limit_bps: 600
                                 }
                               )"),
                               1)
          .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 600
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 400
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 200
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 200
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 300
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 300
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  hipri_rate_limit_bps: 300
                })")));
}

TEST(HeypSigcomm20ClusterAllocatorTest, WithBonus) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: CA_HEYP_SIGCOMM20
                                 enable_burstiness: false
                                 enable_bonus: true
                                 oversub_factor: 1.0
                                 heyp_acceptable_measured_ratio_over_intended_ratio: 0.9
                                 heyp_probe_lopri_when_ambiguous: false
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow { src_dc: "east-us" dst_dc: "west-us" }
                                   hipri_rate_limit_bps: 600
                                 }
                               )"),
                               1)
          .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 450
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 100
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 200
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 150
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 250
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 250
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  hipri_rate_limit_bps: 250
                })")));
}

TEST(HeypSigcomm20ClusterAllocatorTest, WithBurstiness) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: CA_HEYP_SIGCOMM20
                                 enable_burstiness: true
                                 enable_bonus: true
                                 oversub_factor: 1.0
                                 heyp_acceptable_measured_ratio_over_intended_ratio: 0.9
                                 heyp_probe_lopri_when_ambiguous: false
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow { src_dc: "east-us" dst_dc: "west-us" }
                                   hipri_rate_limit_bps: 600
                                 }
                               )"),
                               1)
          .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 600
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 400
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 300
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 200
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 400
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 400
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  hipri_rate_limit_bps: 400
                })")));
}

TEST(HeypSigcomm20ClusterAllocatorTest, WithBurstinessAndCongestion) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: CA_HEYP_SIGCOMM20
                                 enable_burstiness: true
                                 enable_bonus: true
                                 oversub_factor: 1.0
                                 heyp_acceptable_measured_ratio_over_intended_ratio: 0.9
                                 heyp_probe_lopri_when_ambiguous: false
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow { src_dc: "east-us" dst_dc: "west-us" }
                                   hipri_rate_limit_bps: 600
                                 }
                               )"),
                               1)
          .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 800
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 400
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 300
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 200
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 237
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 237
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  hipri_rate_limit_bps: 237
                })")));
}

TEST(HeypSigcomm20ClusterAllocatorTest, ZeroDemand) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: CA_HEYP_SIGCOMM20
                                 enable_burstiness: true
                                 enable_bonus: true
                                 oversub_factor: 1.1
                                 heyp_acceptable_measured_ratio_over_intended_ratio: 0.9
                                 heyp_probe_lopri_when_ambiguous: false
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow { src_dc: "east-us" dst_dc: "west-us" }
                                   hipri_rate_limit_bps: 600
                                 }
                               )"),
                               1)
          .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 50
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 0
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 0
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 330
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 330
                })")));
}

TEST(HeypSigcomm20ClusterAllocatorTest, ZeroLimit) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: CA_HEYP_SIGCOMM20
                                 enable_burstiness: true
                                 enable_bonus: true
                                 oversub_factor: 1.1
                                 heyp_acceptable_measured_ratio_over_intended_ratio: 0.9
                                 heyp_probe_lopri_when_ambiguous: false
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow { src_dc: "east-us" dst_dc: "west-us" }
                                   hipri_rate_limit_bps: 0
                                 }
                               )"),
                               1)
          .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 50
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 50
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 20
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 0
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 0
                })")));
}

TEST(HeypSigcomm20ClusterAllocatorTest, AllLOPRI) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: CA_HEYP_SIGCOMM20
                                 enable_burstiness: true
                                 enable_bonus: true
                                 oversub_factor: 1.1
                                 heyp_acceptable_measured_ratio_over_intended_ratio: 0.9
                                 heyp_probe_lopri_when_ambiguous: false
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow { src_dc: "east-us" dst_dc: "west-us" }
                                   lopri_rate_limit_bps: 50
                                 }
                               )"),
                               1)
          .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 50
                               cum_hipri_usage_bytes: 0
                               cum_lopri_usage_bytes: 380
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 50
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 20
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  lopri_rate_limit_bps: 55
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  lopri_rate_limit_bps: 55
                })")));
}

class LOPRICongestionInfoGenerator {
 public:
  LOPRICongestionInfoGenerator()
      : info_(ParseTextProto<proto::AggInfo>(
            R"(
              parent {
                flow { src_dc: "east-us" dst_dc: "west-us" }
                predicted_demand_bps: 150000
              }
              children {
                flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                predicted_demand_bps: 40000
              }
              children {
                flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                predicted_demand_bps: 30000
              }
              children {
                flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                predicted_demand_bps: 30000
              }
              children {
                flow { src_dc: "east-us" dst_dc: "west-us" host_id: 4 }
                predicted_demand_bps: 20000
              }
              children {
                flow { src_dc: "east-us" dst_dc: "west-us" host_id: 5 }
                predicted_demand_bps: 20000
              }
              children {
                flow { src_dc: "east-us" dst_dc: "west-us" host_id: 6 }
                predicted_demand_bps: 20000
              }
              children {
                flow { src_dc: "east-us" dst_dc: "west-us" host_id: 7 }
                predicted_demand_bps: 5000
              }
              children {
                flow { src_dc: "east-us" dst_dc: "west-us" host_id: 8 }
                predicted_demand_bps: 5000
              }
            )")) {}

  proto::AggInfo AddUsage(int64_t hipri_bytes, int64_t lopri_bytes) {
    auto p = info_.mutable_parent();
    p->set_cum_hipri_usage_bytes(hipri_bytes + p->cum_hipri_usage_bytes());
    p->set_cum_lopri_usage_bytes(lopri_bytes + p->cum_lopri_usage_bytes());
    return info_;
  }

  static int64_t DemandOf(int32_t host_id) {
    switch (host_id) {
      case 1:
        return 40000;
      case 2:
      case 3:
        return 30000;
      case 4:
      case 5:
      case 6:
        return 20000;
        break;
      case 7:
      case 8:
        return 5000;
    }
    std::cerr << "unreachable\n";
    DumpStackTraceAndExit(11);
    return 0;
  }

 private:
  proto::AggInfo info_;
};

TEST(HeypSigcomm20ClusterAllocatorTest, BoundedLOPRICongestion) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: CA_HEYP_SIGCOMM20
                                 enable_burstiness: true
                                 enable_bonus: true
                                 oversub_factor: 1.1
                                 heyp_acceptable_measured_ratio_over_intended_ratio: 0.9
                                 heyp_probe_lopri_when_ambiguous: false
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow { src_dc: "east-us" dst_dc: "west-us" }
                                   hipri_rate_limit_bps: 100000
                                   lopri_rate_limit_bps: 50000
                                 }
                               )"),
                               1.1)
          .value();

  int64_t expected_max_lopri_usage = 0;
  proto::AllocBundle result;
  auto update_bundle_and_collect = [&expected_max_lopri_usage,
                                    &result](const AllocSet& alloc_set) {
    expected_max_lopri_usage = 0;
    result = Bundle(alloc_set);
    for (const proto::FlowAlloc& alloc : result.flow_allocs()) {
      expected_max_lopri_usage +=
          std::min(LOPRICongestionInfoGenerator::DemandOf(alloc.flow().host_id()),
                   alloc.lopri_rate_limit_bps());
    }
  };

  LOPRICongestionInfoGenerator info_gen;

  constexpr double kTotalOversub = 1.2466666667;

  alloc->Reset();
  alloc->AddInfo(T(1), info_gen.AddUsage(10000, 10000));
  update_bundle_and_collect(alloc->GetAllocs());
  // Do not test initial results since we can't revise LOPRI admissions until we
  // set frac_lopri > 0.

  alloc->Reset();
  alloc->AddInfo(T(2), info_gen.AddUsage(16000, 5000));
  update_bundle_and_collect(alloc->GetAllocs());
  EXPECT_THAT(expected_max_lopri_usage,
              testing::AllOf(testing::Ge(40000), testing::Le(kTotalOversub * 40000)));

  alloc->Reset();
  alloc->AddInfo(T(3), info_gen.AddUsage(16000, 4000));
  update_bundle_and_collect(alloc->GetAllocs());
  EXPECT_THAT(expected_max_lopri_usage,
              testing::AllOf(testing::Ge(kTotalOversub * 30000),
                             testing::Le(kTotalOversub * 32000)));

  alloc->Reset();
  alloc->AddInfo(T(4), info_gen.AddUsage(16000, 3990));
  update_bundle_and_collect(alloc->GetAllocs());
  EXPECT_THAT(expected_max_lopri_usage,
              testing::AllOf(testing::Ge(kTotalOversub * 30000),
                             testing::Le(kTotalOversub * 31920)));

  alloc->Reset();
  alloc->AddInfo(T(5), info_gen.AddUsage(16000, 3990));
  update_bundle_and_collect(alloc->GetAllocs());
  EXPECT_THAT(expected_max_lopri_usage,
              testing::AllOf(testing::Ge(kTotalOversub * 30000),
                             testing::Le(kTotalOversub * 31920)));
}

TEST(HeypSigcomm20ClusterAllocatorTest, UnboundedLOPRICongestion) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: CA_HEYP_SIGCOMM20
                                 enable_burstiness: true
                                 enable_bonus: true
                                 oversub_factor: 1.1
                                 heyp_acceptable_measured_ratio_over_intended_ratio: 0.9
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow { src_dc: "east-us" dst_dc: "west-us" }
                                   hipri_rate_limit_bps: 100000
                                   lopri_rate_limit_bps: 50000
                                 }
                               )"),
                               1)
          .value();

  int64_t expected_max_lopri_usage = 0;
  proto::AllocBundle result;
  auto update_bundle_and_collect = [&expected_max_lopri_usage,
                                    &result](const AllocSet& alloc_set) {
    expected_max_lopri_usage = 0;
    result = Bundle(alloc_set);
    for (const proto::FlowAlloc& alloc : result.flow_allocs()) {
      expected_max_lopri_usage +=
          std::min(LOPRICongestionInfoGenerator::DemandOf(alloc.flow().host_id()),
                   alloc.lopri_rate_limit_bps());
    }
  };

  LOPRICongestionInfoGenerator info_gen;

  constexpr double kTotalOversub = 1.2466666667;

  alloc->Reset();
  alloc->AddInfo(T(1), info_gen.AddUsage(10000, 10000));
  update_bundle_and_collect(alloc->GetAllocs());
  // Do not test initial results since we can't revise LOPRI admissions until we
  // set frac_lopri > 0.

  alloc->Reset();
  alloc->AddInfo(T(2), info_gen.AddUsage(16000, 5000));
  update_bundle_and_collect(alloc->GetAllocs());
  EXPECT_THAT(expected_max_lopri_usage,
              testing::AllOf(testing::Ge(40000), testing::Le(kTotalOversub * 40000)));

  alloc->Reset();
  alloc->AddInfo(T(3), info_gen.AddUsage(16000, 4000));
  update_bundle_and_collect(alloc->GetAllocs());
  EXPECT_THAT(expected_max_lopri_usage,
              testing::AllOf(testing::Ge(30000), testing::Le(kTotalOversub * 32000)));

  alloc->Reset();
  alloc->AddInfo(T(4), info_gen.AddUsage(16000, 3000));
  update_bundle_and_collect(alloc->GetAllocs());
  EXPECT_THAT(expected_max_lopri_usage,
              testing::AllOf(testing::Ge(24000), testing::Le(kTotalOversub * 24000)));

  alloc->Reset();
  alloc->AddInfo(T(5), info_gen.AddUsage(16000, 2000));
  update_bundle_and_collect(alloc->GetAllocs());
  EXPECT_THAT(expected_max_lopri_usage,
              testing::AllOf(testing::Ge(15000), testing::Le(kTotalOversub * 16000)));

  alloc->Reset();
  alloc->AddInfo(T(6), info_gen.AddUsage(16000, 1000));
  update_bundle_and_collect(alloc->GetAllocs());
  EXPECT_THAT(expected_max_lopri_usage,
              testing::AllOf(testing::Ge(5000), testing::Le(kTotalOversub * 8000)));

  alloc->Reset();
  alloc->AddInfo(T(7), info_gen.AddUsage(16000, 500));
  update_bundle_and_collect(alloc->GetAllocs());
  EXPECT_THAT(expected_max_lopri_usage, testing::Le(kTotalOversub * 4000));

  alloc->Reset();
  alloc->AddInfo(T(8), info_gen.AddUsage(16000, 0));
  update_bundle_and_collect(alloc->GetAllocs());
  EXPECT_THAT(expected_max_lopri_usage, testing::Eq(0));
}

TEST(SimpleDowngradeClusterAllocatorTest, MissingAllocInConfig) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_SIMPLE_DOWNGRADE
                                          enable_burstiness: false
                                          enable_bonus: true
                                          oversub_factor: 1.0
                                          downgrade_selector { type: DS_KNAPSACK_SOLVER }
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 666666
                                            lopri_rate_limit_bps: 333333
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent { flow { src_dc: "east-us" dst_dc: "central-us" } }
                             children {
                               flow { src_dc: "east-us" dst_dc: "central-us" host_id: 1 }
                               predicted_demand_bps: 600000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "central-us" host_id: 2 }
                               predicted_demand_bps: 60000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "central-us" host_id: 3 }
                               predicted_demand_bps: 6000
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()), AllocBundleEq(proto::AllocBundle()));
}

TEST(SimpleDowngradeClusterAllocatorTest, Basic) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_SIMPLE_DOWNGRADE
                                          enable_burstiness: false
                                          enable_bonus: true
                                          oversub_factor: 1.0
                                          downgrade_selector { type: DS_KNAPSACK_SOLVER }
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 600000
                                            lopri_rate_limit_bps: 333333
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 933300
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 600000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 300000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 30000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 4 }
                               predicted_demand_bps: 3000
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 5 }
                               predicted_demand_bps: 300
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 600000
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  lopri_rate_limit_bps: 300008
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  lopri_rate_limit_bps: 300008
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 4 }
                  lopri_rate_limit_bps: 300008
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 5 }
                  lopri_rate_limit_bps: 300008
                })")));
}

TEST(SimpleDowngradeClusterAllocatorTest, WithOversub) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_SIMPLE_DOWNGRADE
                                          enable_burstiness: false
                                          enable_bonus: true
                                          oversub_factor: 1.5
                                          downgrade_selector { type: DS_KNAPSACK_SOLVER }
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 600
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 600
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 400
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 200
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 200
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 300
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 300
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  hipri_rate_limit_bps: 300
                })")));
}

TEST(SimpleDowngradeClusterAllocatorTest, WithBonus) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_SIMPLE_DOWNGRADE
                                          enable_burstiness: false
                                          enable_bonus: true
                                          oversub_factor: 1.0
                                          downgrade_selector { type: DS_KNAPSACK_SOLVER }
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 600
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 450
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 100
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 200
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 150
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 250
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 250
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  hipri_rate_limit_bps: 250
                })")));
}

TEST(SimpleDowngradeClusterAllocatorTest, WithBurstiness) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_SIMPLE_DOWNGRADE
                                          enable_burstiness: true
                                          enable_bonus: true
                                          oversub_factor: 1.0
                                          downgrade_selector { type: DS_KNAPSACK_SOLVER }
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 600
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 600
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 400
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 300
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 200
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 400
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 400
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  hipri_rate_limit_bps: 400
                })")));
}

TEST(SimpleDowngradeClusterAllocatorTest, WithBurstinessAndCongestion) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_SIMPLE_DOWNGRADE
                                          enable_burstiness: true
                                          enable_bonus: true
                                          oversub_factor: 1.0
                                          downgrade_selector { type: DS_KNAPSACK_SOLVER }
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 600
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 800
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 400
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 300
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                               predicted_demand_bps: 200
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 375
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 375
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 3 }
                  lopri_rate_limit_bps: 0
                })")));
}

TEST(SimpleDowngradeClusterAllocatorTest, ZeroDemand) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_SIMPLE_DOWNGRADE
                                          enable_burstiness: true
                                          enable_bonus: true
                                          oversub_factor: 1.1
                                          downgrade_selector { type: DS_KNAPSACK_SOLVER }
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 600
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 50
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 0
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 0
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 330
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 330
                })")));
}

TEST(SimpleDowngradeClusterAllocatorTest, ZeroLimit) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_SIMPLE_DOWNGRADE
                                          enable_burstiness: true
                                          enable_bonus: true
                                          oversub_factor: 1.1
                                          downgrade_selector { type: DS_KNAPSACK_SOLVER }
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            hipri_rate_limit_bps: 0
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 50
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 50
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 20
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  hipri_rate_limit_bps: 0
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  hipri_rate_limit_bps: 0
                })")));
}

TEST(SimpleDowngradeClusterAllocatorTest, AllLOPRI) {
  auto alloc = ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                          type: CA_SIMPLE_DOWNGRADE
                                          enable_burstiness: true
                                          enable_bonus: true
                                          oversub_factor: 1.1
                                          downgrade_selector { type: DS_KNAPSACK_SOLVER }
                                        )"),
                                        ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow { src_dc: "east-us" dst_dc: "west-us" }
                                            lopri_rate_limit_bps: 50
                                          }
                                        )"),
                                        1)
                   .value();
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow { src_dc: "east-us" dst_dc: "west-us" }
                               predicted_demand_bps: 50
                               cum_hipri_usage_bytes: 0
                               cum_lopri_usage_bytes: 380
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                               predicted_demand_bps: 50
                             }
                             children {
                               flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                               predicted_demand_bps: 20
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                  lopri_rate_limit_bps: 55
                }
                flow_allocs {
                  flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                  lopri_rate_limit_bps: 55
                })")));
}

}  // namespace
}  // namespace heyp
