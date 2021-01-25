#include "heyp/cluster-agent/allocator.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/parse-text.h"
#include "heyp/proto/testing.h"

namespace heyp {
namespace {

absl::Time T(int64_t sec) { return absl::UnixEpoch() + absl::Seconds(1); }

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

TEST(BweClusterAllocatorTest, Basic) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: BWE
                                 enable_burstiness: false
                                 enable_bonus: true
                                 oversub_factor: 1.0
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow {
                                     src_dc: "east-us"
                                     dst_dc: "west-us"
                                   }
                                   hipri_rate_limit_bps: 666666
                                 }
                               )"));
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                               }
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 1
                               }
                               predicted_demand_bps: 600000
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 2
                               }
                               predicted_demand_bps: 60000
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 3
                               }
                               predicted_demand_bps: 6000
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 4
                               }
                               predicted_demand_bps: 600
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 5
                               }
                               predicted_demand_bps: 67
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 1
                  }
                  hipri_rate_limit_bps: 599999
                }
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 2
                  }
                  hipri_rate_limit_bps: 599999
                }
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 3
                  }
                  hipri_rate_limit_bps: 599999
                }
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 4
                  }
                  hipri_rate_limit_bps: 599999
                }
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 5
                  }
                  hipri_rate_limit_bps: 599999
                })")));
}

TEST(BweClusterAllocatorTest, WithOversub) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: BWE
                                 enable_burstiness: false
                                 enable_bonus: true
                                 oversub_factor: 1.5
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow {
                                     src_dc: "east-us"
                                     dst_dc: "west-us"
                                   }
                                   hipri_rate_limit_bps: 600
                                 }
                               )"));
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                               }
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 1
                               }
                               predicted_demand_bps: 400
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 2
                               }
                               predicted_demand_bps: 200
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 3
                               }
                               predicted_demand_bps: 200
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 1
                  }
                  hipri_rate_limit_bps: 300
                }
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 2
                  }
                  hipri_rate_limit_bps: 300
                }
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 3
                  }
                  hipri_rate_limit_bps: 300
                })")));
}

TEST(BweClusterAllocatorTest, WithBonus) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: BWE
                                 enable_burstiness: false
                                 enable_bonus: true
                                 oversub_factor: 1.0
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow {
                                     src_dc: "east-us"
                                     dst_dc: "west-us"
                                   }
                                   hipri_rate_limit_bps: 600
                                 }
                               )"));
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                               }
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 1
                               }
                               predicted_demand_bps: 100
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 2
                               }
                               predicted_demand_bps: 200
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 3
                               }
                               predicted_demand_bps: 150
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 1
                  }
                  hipri_rate_limit_bps: 250
                }
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 2
                  }
                  hipri_rate_limit_bps: 250
                }
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 3
                  }
                  hipri_rate_limit_bps: 250
                })")));
}

TEST(BweClusterAllocatorTest, WithBurstiness) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: BWE
                                 enable_burstiness: true
                                 enable_bonus: true
                                 oversub_factor: 1.0
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow {
                                     src_dc: "east-us"
                                     dst_dc: "west-us"
                                   }
                                   hipri_rate_limit_bps: 600
                                 }
                               )"));
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                               }
                               predicted_demand_bps: 600
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 1
                               }
                               predicted_demand_bps: 400
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 2
                               }
                               predicted_demand_bps: 300
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 3
                               }
                               predicted_demand_bps: 200
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 1
                  }
                  hipri_rate_limit_bps: 400
                }
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 2
                  }
                  hipri_rate_limit_bps: 400
                }
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 3
                  }
                  hipri_rate_limit_bps: 400
                })")));
}

TEST(BweClusterAllocatorTest, WithBurstinessAndCongestion) {
  auto alloc =
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                 type: BWE
                                 enable_burstiness: true
                                 enable_bonus: true
                                 oversub_factor: 1.0
                               )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                 flow_allocs {
                                   flow {
                                     src_dc: "east-us"
                                     dst_dc: "west-us"
                                   }
                                   hipri_rate_limit_bps: 600
                                 }
                               )"));
  alloc->Reset();
  alloc->AddInfo(T(1), ParseTextProto<proto::AggInfo>(
                           R"(
                             parent {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                               }
                               predicted_demand_bps: 800
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 1
                               }
                               predicted_demand_bps: 400
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 2
                               }
                               predicted_demand_bps: 300
                             }
                             children {
                               flow {
                                 src_dc: "east-us"
                                 dst_dc: "west-us"
                                 host_id: 3
                               }
                               predicted_demand_bps: 200
                             }
                           )"));
  EXPECT_THAT(Bundle(alloc->GetAllocs()),
              AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 1
                  }
                  hipri_rate_limit_bps: 237
                }
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 2
                  }
                  hipri_rate_limit_bps: 237
                }
                flow_allocs {
                  flow {
                    src_dc: "east-us"
                    dst_dc: "west-us"
                    host_id: 3
                  }
                  hipri_rate_limit_bps: 237
                })")));
}

// TODO: test HeypSigcomm20

}  // namespace
}  // namespace heyp

// TODO:
// - Test for 100% LOPRI
// - Test for 100% HIPRI
// - Test for no HIPRI usage
// - Test for no LOPRI usage
// - Test for zero demand
// - Test for zero usage
// - Test for zero limit
