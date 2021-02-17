#include "heyp/cluster-agent/controller.h"

#include "absl/memory/memory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/parse-text.h"
#include "heyp/proto/testing.h"

namespace heyp {
namespace {

ClusterController MakeClusterController() {
  return ClusterController(
      NewHostToClusterAggregator(
          absl::make_unique<BweDemandPredictor>(absl::Seconds(5), 1.0, 500),
          absl::Seconds(5)),
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                  type: HEYP_SIGCOMM20
                                  enable_burstiness: true
                                  enable_bonus: true
                                  oversub_factor: 1.0
                                  heyp_acceptable_measured_ratio_over_intended_ratio: 1.0
                                  )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                  flow_allocs {
                                    flow {
                                      src_dc: "chicago"
                                      dst_dc: "new_york"
                                    }
                                    hipri_rate_limit_bps: 1000
                                  }
                                  flow_allocs {
                                    flow {
                                      src_dc: "chicago"
                                      dst_dc: "detroit"
                                    }
                                    hipri_rate_limit_bps: 1000
                                    lopri_rate_limit_bps: 1000
                                  }
                                  )")));
}

TEST(ClusterControllerTest, EmptyListenerDestroysCorrectly) {
  ClusterController::Listener lis;
}

TEST(ClusterControllerTest, MoveListener) {
  auto controller = MakeClusterController();
  ClusterController::Listener lis1 =
      controller.RegisterListener(1, [](const proto::AllocBundle&) {});
  ClusterController::Listener lis2 =
      controller.RegisterListener(2, [](const proto::AllocBundle&) {});

  // Now move the listeners

  ClusterController::Listener new_lis1 = std::move(lis1);
  ClusterController::Listener new_lis2(std::move(lis2));

  // One more time, just in case

  ClusterController::Listener new_new_lis1 = std::move(new_lis1);
  ClusterController::Listener new_new_lis2(std::move(new_lis2));
}

TEST(ClusterControllerTest, PlumbsDataCompletely) {
  auto controller = MakeClusterController();

  int call_count = 0;
  auto lis1 = controller.RegisterListener(1, [&call_count](const proto::AllocBundle& b1) {
    EXPECT_THAT(b1, AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow {
                                              src_dc: "chicago"
                                              dst_dc: "new_york"
                                              host_id: 1
                                            }
                                            hipri_rate_limit_bps: 1000
                                          }
                                          flow_allocs {
                                            flow {
                                              src_dc: "chicago"
                                              dst_dc: "detroit"
                                              host_id: 1
                                            }
                                            lopri_rate_limit_bps: 1000
                                          }
                                          )")));
    ++call_count;
  });
  auto lis2 = controller.RegisterListener(2, [&call_count](const proto::AllocBundle& b2) {
    EXPECT_THAT(b2, AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow {
                                              src_dc: "chicago"
                                              dst_dc: "detroit"
                                              host_id: 2
                                            }
                                            hipri_rate_limit_bps: 1000
                                          }
                                          )")));
    ++call_count;
  });

  controller.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
                            bundler {
                              host_id: 1
                            }
                            timestamp {
                              seconds: 1
                            }
                            flow_infos {
                              flow {
                                src_dc: "chicago"
                                dst_dc: "detroit"
                                host_id: 1
                              }
                              predicted_demand_bps: 1000
                              ewma_usage_bps: 1000
                              currently_lopri: true
                            }
                            flow_infos {
                              flow {
                                src_dc: "chicago"
                                dst_dc: "new_york"
                                host_id: 1
                              }
                              predicted_demand_bps: 1000
                              ewma_usage_bps: 1000
                            }
                            )"));
  controller.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
                            bundler {
                              host_id: 2
                            }
                            timestamp {
                              seconds: 1
                            }
                            flow_infos {
                              flow {
                                src_dc: "chicago"
                                dst_dc: "detroit"
                                host_id: 2
                              }
                              predicted_demand_bps: 1000
                              ewma_usage_bps: 1000
                            }
                            )"));
  controller.ComputeAndBroadcast();

  EXPECT_EQ(call_count, 2);
}

}  // namespace
}  // namespace heyp
