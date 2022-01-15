#include "heyp/cluster-agent/fast-controller.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/proto/parse-text.h"
#include "heyp/proto/testing.h"

namespace heyp {
namespace {

std::unique_ptr<FastClusterController> MakeFastClusterController() {
  proto::FastClusterControllerConfig config;
  config.set_target_num_samples(10);
  config.set_num_threads(3);

  return FastClusterController::Create(config, ParseTextProto<proto::AllocBundle>(R"(
                                         flow_allocs {
                                           flow { src_dc: "chicago" dst_dc: "new_york" }
                                           hipri_rate_limit_bps: 1000
                                         }
                                         flow_allocs {
                                           flow { src_dc: "chicago" dst_dc: "detroit" }
                                           hipri_rate_limit_bps: 500
                                         }
                                       )"));
}

void UpdateInfo(FastClusterController* c, const proto::InfoBundle& b) {
  ParID id = c->GetBundlerID(b.bundler());
  c->UpdateInfo(id, b);
}

TEST(FastClusterControllerTest, RemoveListener) {
  auto controller = MakeFastClusterController();

  int num_broadcast_1 = 0;
  int num_broadcast_1_1 = 0;
  int num_broadcast_2 = 0;
  int num_broadcast_3 = 0;

  auto lis1 = controller->RegisterListener(
      1, [&](const proto::AllocBundle&, const SendBundleAux&) { ++num_broadcast_1; });
  auto lis1_1 = controller->RegisterListener(
      1, [&](const proto::AllocBundle&, const SendBundleAux&) { ++num_broadcast_1_1; });
  auto lis2 = controller->RegisterListener(
      2, [&](const proto::AllocBundle&, const SendBundleAux&) { ++num_broadcast_2; });
  auto lis3 = controller->RegisterListener(
      3, [&](const proto::AllocBundle&, const SendBundleAux&) { ++num_broadcast_3; });

  // Update some infos

  UpdateInfo(controller.get(), ParseTextProto<proto::InfoBundle>(R"(
               bundler { host_id: 1 }
               timestamp { seconds: 1 }
               flow_infos {
                 flow { src_dc: "chicago" dst_dc: "detroit" job: "UNSET" host_id: 1 }
                 predicted_demand_bps: 1000
                 ewma_usage_bps: 1000
                 currently_lopri: true
               }
               flow_infos {
                 flow { src_dc: "chicago" dst_dc: "new_york" job: "UNSET" host_id: 1 }
                 predicted_demand_bps: 1000
                 ewma_usage_bps: 1000
               }
             )"));
  UpdateInfo(controller.get(), ParseTextProto<proto::InfoBundle>(R"(
               bundler { host_id: 2 }
               timestamp { seconds: 1 }
               flow_infos {
                 flow { src_dc: "chicago" dst_dc: "detroit" job: "UNSET" host_id: 2 }
                 predicted_demand_bps: 1000
                 ewma_usage_bps: 1000
               }
             )"));

  controller->ComputeAndBroadcast();

  EXPECT_EQ(num_broadcast_1, 1);
  EXPECT_EQ(num_broadcast_1_1, 1);
  EXPECT_EQ(num_broadcast_2, 1);
  EXPECT_EQ(num_broadcast_3, 1);

  // Now delete some of the listeners

  lis1 = nullptr;
  lis2 = nullptr;

  // Update infos again

  UpdateInfo(controller.get(), ParseTextProto<proto::InfoBundle>(R"(
               bundler { host_id: 1 }
               timestamp { seconds: 1 }
               flow_infos {
                 flow { src_dc: "chicago" dst_dc: "detroit" job: "UNSET" host_id: 1 }
                 predicted_demand_bps: 0
                 ewma_usage_bps: 0
                 currently_lopri: true
               }
               flow_infos {
                 flow { src_dc: "chicago" dst_dc: "new_york" job: "UNSET" host_id: 1 }
                 predicted_demand_bps: 0
                 ewma_usage_bps: 0
               }
             )"));
  UpdateInfo(controller.get(), ParseTextProto<proto::InfoBundle>(R"(
               bundler { host_id: 2 }
               timestamp { seconds: 1 }
               flow_infos {
                 flow { src_dc: "chicago" dst_dc: "detroit" job: "UNSET" host_id: 2 }
                 predicted_demand_bps: 0
                 ewma_usage_bps: 0
               }
             )"));

  controller->ComputeAndBroadcast();

  EXPECT_EQ(num_broadcast_1, 1);
  EXPECT_EQ(num_broadcast_1_1, 2);
  EXPECT_EQ(num_broadcast_2, 1);
  EXPECT_EQ(num_broadcast_3, 2);
}

TEST(FastClusterControllerTest, PlumbsDataCompletely) {
  auto controller = MakeFastClusterController();

  std::atomic<int> call_count = 0;
  auto lis1 = controller->RegisterListener(
      1, [&call_count](const proto::AllocBundle& b1, const SendBundleAux& aux) {
        EXPECT_THAT(b1, AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                      flow_allocs {
                        flow { src_dc: "chicago" dst_dc: "new_york" }
                        hipri_rate_limit_bps: 107374182400
                      }
                      flow_allocs {
                        flow { src_dc: "chicago" dst_dc: "detroit" }
                        lopri_rate_limit_bps: 107374182400
                      }
                    )")));
        ++call_count;
      });
  auto lis2 = controller->RegisterListener(
      2, [&call_count](const proto::AllocBundle& b2, const SendBundleAux& aux) {
        EXPECT_THAT(b2, AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                      flow_allocs {
                        flow { src_dc: "chicago" dst_dc: "new_york" }
                        hipri_rate_limit_bps: 107374182400
                      }
                      flow_allocs {
                        flow { src_dc: "chicago" dst_dc: "detroit" }
                        lopri_rate_limit_bps: 107374182400
                      }
                    )")));
        ++call_count;
      });
  auto lis3 = controller->RegisterListener(
      3, [&call_count](const proto::AllocBundle& b3, const SendBundleAux& aux) {
        EXPECT_THAT(b3, AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                      flow_allocs {
                        flow { src_dc: "chicago" dst_dc: "new_york" }
                        hipri_rate_limit_bps: 107374182400
                      }
                      flow_allocs {
                        flow { src_dc: "chicago" dst_dc: "detroit" }
                        lopri_rate_limit_bps: 107374182400
                      }
                    )")));
        ++call_count;
      });
  auto lis4 = controller->RegisterListener(
      9223372036854775809UL,
      [&call_count](const proto::AllocBundle& b4, const SendBundleAux& aux) {
        EXPECT_THAT(b4, AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                      flow_allocs {
                        flow { src_dc: "chicago" dst_dc: "new_york" }
                        hipri_rate_limit_bps: 107374182400
                      }
                      flow_allocs {
                        flow { src_dc: "chicago" dst_dc: "detroit" }
                        hipri_rate_limit_bps: 107374182400
                      }
                    )")));
        ++call_count;
      });

  // Chicago->Detroit: Observed 10, 500, 310, and 100
  // Estimate Usage: 10 * (1/10) = 100 + 500 + 310 + 100 = 1010
  // Should be enough to downgrade first three hosts.
  UpdateInfo(controller.get(), ParseTextProto<proto::InfoBundle>(R"(
               bundler { host_id: 1 }
               timestamp { seconds: 1 }
               flow_infos {
                 flow { src_dc: "chicago" dst_dc: "detroit" job: "UNSET" host_id: 1 }
                 predicted_demand_bps: 1000  # should be ignored
                 ewma_usage_bps: 10
                 currently_lopri: true
               }
               flow_infos {
                 flow { src_dc: "chicago" dst_dc: "new_york" job: "UNSET" host_id: 1 }
                 predicted_demand_bps: 1000  # should be ignored
                 ewma_usage_bps: 500
               }
             )"));
  UpdateInfo(controller.get(), ParseTextProto<proto::InfoBundle>(R"(
               bundler { host_id: 2 }
               timestamp { seconds: 1 }
               flow_infos {
                 flow { src_dc: "chicago" dst_dc: "detroit" job: "UNSET" host_id: 2 }
                 predicted_demand_bps: 1000  # should be ignored
                 ewma_usage_bps: 500
                 currently_lopri: true
               }
             )"));
  UpdateInfo(controller.get(), ParseTextProto<proto::InfoBundle>(R"(
               bundler { host_id: 3 }
               timestamp { seconds: 1 }
               flow_infos {
                 flow { src_dc: "chicago" dst_dc: "detroit" job: "UNSET" host_id: 3 }
                 predicted_demand_bps: 1000  # should be ignored
                 ewma_usage_bps: 310
                 currently_lopri: true
               }
             )"));
  UpdateInfo(controller.get(), ParseTextProto<proto::InfoBundle>(R"(
               bundler { host_id: 9223372036854775808 }
               timestamp { seconds: 1 }
               flow_infos {
                 flow {
                   src_dc: "chicago"
                   dst_dc: "detroit"
                   job: "UNSET"
                   host_id: 9223372036854775809
                 }
                 predicted_demand_bps: 1000  # should be ignored
                 ewma_usage_bps: 100
               }
             )"));
  controller->ComputeAndBroadcast();

  EXPECT_EQ(call_count, 3);
}

}  // namespace
}  // namespace heyp
