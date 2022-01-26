#include "heyp/cluster-agent/fast-aggregator.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/proto/parse-text.h"
#include "heyp/proto/testing.h"

namespace heyp {
namespace {

proto::FlowMarker TestAggFlow(int i) {
  proto::FlowMarker flow;
  flow.set_src_dc("A");
  flow.set_dst_dc(absl::StrCat("B-", i));
  return flow;
}

ClusterFlowMap<int64_t> TestAggFlowToIdMap() {
  return ClusterFlowMap<int64_t>{
      {TestAggFlow(0), 0},
      {TestAggFlow(1), 1},
      {TestAggFlow(2), 2},
  };
}

std::vector<HashingDowngradeSelector> TestDowngradeSelectors() {
  return std::vector<HashingDowngradeSelector>{
      HashingDowngradeSelector(),
      HashingDowngradeSelector(),
      HashingDowngradeSelector(),
  };
}

std::vector<ThresholdSampler> TestSamplers(int num_samples, int64_t approval0_bps,
                                           int64_t approval1_bps, int64_t approval2_bps) {
  return std::vector<ThresholdSampler>{
      ThresholdSampler(num_samples, approval0_bps),
      ThresholdSampler(num_samples, approval1_bps),
      ThresholdSampler(num_samples, approval2_bps),
  };
}

TEST(FastAggregatorTest, NoSamplingOneFG) {
  const ClusterFlowMap<int64_t> agg_flow_to_id = TestAggFlowToIdMap();

  FastAggregator aggregator(&agg_flow_to_id, TestSamplers(100, 0, 0, 0));
  aggregator.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 101 }
    flow_infos {
      flow { src_dc: "A" dst_dc: "B-1" job: "web" host_id: 101 }
      ewma_usage_bps: 100
    }
  )"));
  aggregator.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 999 }
    flow_infos {
      flow { src_dc: "A" dst_dc: "B-1" job: "web" host_id: 999 }
      ewma_usage_bps: 202
    }
  )"));
  aggregator.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 44 }
    flow_infos {
      flow { src_dc: "A" dst_dc: "B-1" job: "web" host_id: 44 }
      ewma_usage_bps: 50
      currently_lopri: true
    }
  )"));

  Executor exec(4);
  std::vector<FastAggInfo> agg_info =
      aggregator.CollectSnapshot(&exec, TestDowngradeSelectors());

  EXPECT_EQ(agg_info.size(), 3);

  EXPECT_EQ(agg_info[0].agg_id(), 0);
  EXPECT_THAT(agg_info[0].parent().flow(), EqProto(TestAggFlow(0)));
  EXPECT_EQ(agg_info[0].parent().ewma_usage_bps(), 0);
  EXPECT_EQ(agg_info[0].parent().ewma_hipri_usage_bps(), 0);
  EXPECT_EQ(agg_info[0].parent().ewma_lopri_usage_bps(), 0);
  EXPECT_THAT(agg_info[0].children(), testing::IsEmpty());

  EXPECT_EQ(agg_info[1].agg_id(), 1);
  EXPECT_THAT(agg_info[1].parent().flow(), EqProto(TestAggFlow(1)));
  EXPECT_EQ(agg_info[1].parent().ewma_usage_bps(), 352);
  EXPECT_EQ(agg_info[1].parent().ewma_hipri_usage_bps(), 352);
  EXPECT_EQ(agg_info[1].parent().ewma_lopri_usage_bps(), 0);
  EXPECT_THAT(
      agg_info[1].children(),
      testing::UnorderedElementsAre(
          ChildFlowInfo{.child_id = 101, .volume_bps = 100, .currently_lopri = false},
          ChildFlowInfo{.child_id = 999, .volume_bps = 202, .currently_lopri = false},
          ChildFlowInfo{.child_id = 44, .volume_bps = 50, .currently_lopri = true}));

  EXPECT_EQ(agg_info[2].agg_id(), 2);
  EXPECT_THAT(agg_info[2].parent().flow(), EqProto(TestAggFlow(2)));
  EXPECT_EQ(agg_info[2].parent().ewma_usage_bps(), 0);
  EXPECT_EQ(agg_info[2].parent().ewma_hipri_usage_bps(), 0);
  EXPECT_EQ(agg_info[2].parent().ewma_lopri_usage_bps(), 0);
  EXPECT_THAT(agg_info[2].children(), testing::IsEmpty());
}

TEST(FastAggregatorTest, NoSamplingMultiFG) {
  const ClusterFlowMap<int64_t> agg_flow_to_id = TestAggFlowToIdMap();

  FastAggregator aggregator(&agg_flow_to_id, TestSamplers(100, 0, 0, 0));
  aggregator.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 101 }
    flow_infos {
      flow { src_dc: "A" dst_dc: "B-1" job: "web" host_id: 101 }
      ewma_usage_bps: 100
    }
  )"));
  aggregator.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 999 }
    flow_infos {
      flow { src_dc: "A" dst_dc: "B-2" job: "web" host_id: 999 }
      ewma_usage_bps: 202
    }
  )"));
  aggregator.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 44 }
    flow_infos {
      flow { src_dc: "A" dst_dc: "B-0" job: "web" host_id: 44 }
      ewma_usage_bps: 50
      currently_lopri: true
    }
  )"));

  Executor exec(4);
  std::vector<FastAggInfo> agg_info =
      aggregator.CollectSnapshot(&exec, TestDowngradeSelectors());

  EXPECT_EQ(agg_info.size(), 3);

  EXPECT_EQ(agg_info[0].agg_id(), 0);
  EXPECT_THAT(agg_info[0].parent().flow(), EqProto(TestAggFlow(0)));
  EXPECT_EQ(agg_info[0].parent().ewma_usage_bps(), 50);
  EXPECT_EQ(agg_info[0].parent().ewma_hipri_usage_bps(), 50);
  EXPECT_EQ(agg_info[0].parent().ewma_lopri_usage_bps(), 0);
  EXPECT_THAT(agg_info[0].children(),
              testing::UnorderedElementsAre(ChildFlowInfo{
                  .child_id = 44, .volume_bps = 50, .currently_lopri = true}));

  EXPECT_EQ(agg_info[1].agg_id(), 1);
  EXPECT_THAT(agg_info[1].parent().flow(), EqProto(TestAggFlow(1)));
  EXPECT_EQ(agg_info[1].parent().ewma_usage_bps(), 100);
  EXPECT_EQ(agg_info[1].parent().ewma_hipri_usage_bps(), 100);
  EXPECT_EQ(agg_info[1].parent().ewma_lopri_usage_bps(), 0);
  EXPECT_THAT(agg_info[1].children(),
              testing::UnorderedElementsAre(ChildFlowInfo{
                  .child_id = 101, .volume_bps = 100, .currently_lopri = false}));

  EXPECT_EQ(agg_info[2].agg_id(), 2);
  EXPECT_THAT(agg_info[2].parent().flow(), EqProto(TestAggFlow(2)));
  EXPECT_EQ(agg_info[2].parent().ewma_usage_bps(), 202);
  EXPECT_EQ(agg_info[2].parent().ewma_hipri_usage_bps(), 202);
  EXPECT_EQ(agg_info[2].parent().ewma_lopri_usage_bps(), 0);
  EXPECT_THAT(agg_info[2].children(),
              testing::UnorderedElementsAre(ChildFlowInfo{
                  .child_id = 999, .volume_bps = 202, .currently_lopri = false}));
}

TEST(FastAggregatorTest, WithSamplingOneFG) {
  const ClusterFlowMap<int64_t> agg_flow_to_id = TestAggFlowToIdMap();

  FastAggregator aggregator(&agg_flow_to_id, TestSamplers(10, 100, 0, 0));
  aggregator.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 101 }
    flow_infos {
      flow { src_dc: "A" dst_dc: "B-0" job: "web" host_id: 101 }
      ewma_usage_bps: 100
    }
  )"));
  aggregator.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 999 }
    flow_infos {
      flow { src_dc: "A" dst_dc: "B-0" job: "web" host_id: 999 }
      ewma_usage_bps: 202
    }
  )"));
  aggregator.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 44 }
    flow_infos {
      flow { src_dc: "A" dst_dc: "B-0" job: "web" host_id: 44 }
      ewma_usage_bps: 50
      currently_lopri: true
    }
  )"));
  aggregator.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 4 }
    flow_infos {
      flow { src_dc: "A" dst_dc: "B-0" job: "web" host_id: 4 }
      ewma_usage_bps: 4
      currently_lopri: true
    }
  )"));

  Executor exec(4);
  std::vector<FastAggInfo> agg_info =
      aggregator.CollectSnapshot(&exec, TestDowngradeSelectors());

  EXPECT_EQ(agg_info.size(), 3);

  EXPECT_EQ(agg_info[0].agg_id(), 0);
  EXPECT_THAT(agg_info[0].parent().flow(), EqProto(TestAggFlow(0)));
  EXPECT_EQ(agg_info[0].parent().ewma_usage_bps(), 362);
  EXPECT_EQ(agg_info[0].parent().ewma_hipri_usage_bps(), 362);
  EXPECT_EQ(agg_info[0].parent().ewma_lopri_usage_bps(), 0);
  EXPECT_THAT(
      agg_info[0].children(),
      testing::UnorderedElementsAre(
          ChildFlowInfo{.child_id = 101, .volume_bps = 100, .currently_lopri = false},
          ChildFlowInfo{.child_id = 999, .volume_bps = 202, .currently_lopri = false},
          ChildFlowInfo{.child_id = 44, .volume_bps = 50, .currently_lopri = true},
          ChildFlowInfo{.child_id = 4, .volume_bps = 4, .currently_lopri = true}));

  EXPECT_EQ(agg_info[1].agg_id(), 1);
  EXPECT_THAT(agg_info[1].parent().flow(), EqProto(TestAggFlow(1)));
  EXPECT_EQ(agg_info[1].parent().ewma_usage_bps(), 0);
  EXPECT_EQ(agg_info[1].parent().ewma_hipri_usage_bps(), 0);
  EXPECT_EQ(agg_info[1].parent().ewma_lopri_usage_bps(), 0);
  EXPECT_THAT(agg_info[1].children(), testing::IsEmpty());

  EXPECT_EQ(agg_info[2].agg_id(), 2);
  EXPECT_THAT(agg_info[2].parent().flow(), EqProto(TestAggFlow(2)));
  EXPECT_EQ(agg_info[2].parent().ewma_usage_bps(), 0);
  EXPECT_EQ(agg_info[2].parent().ewma_hipri_usage_bps(), 0);
  EXPECT_EQ(agg_info[2].parent().ewma_lopri_usage_bps(), 0);
  EXPECT_THAT(agg_info[2].children(), testing::IsEmpty());
}

}  // namespace
}  // namespace heyp
