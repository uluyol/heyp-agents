#include "heyp/alg/agg-info-views.h"

#include "flow-volume.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/parse-text.h"
#include "heyp/proto/testing.h"

namespace heyp {
namespace {

TEST(HostLevelViewTest, Basic) {
  auto raw_info = ParseTextProto<proto::AggInfo>(R"(
    parent {
      flow { src_dc: "NY" dst_dc: "LA" }
      predicted_demand_bps: 1234
      ewma_usage_bps: 1111
      cum_usage_bytes: 9099
      cum_hipri_usage_bytes: 9000
      cum_lopri_usage_bytes: 99
      currently_lopri: false
    }
    children {
      flow { src_dc: "NY" dst_dc: "LA" job: "YT" host_id: 1 }
      predicted_demand_bps: 500
      ewma_usage_bps: 500
      cum_usage_bytes: 100
      cum_hipri_usage_bytes: 1
      cum_lopri_usage_bytes: 99
      currently_lopri: true
    }
    children {
      flow { src_dc: "NY" dst_dc: "LA" job: "FB" host_id: 2 }
      predicted_demand_bps: 300
      ewma_usage_bps: 300
      cum_usage_bytes: 99
      cum_hipri_usage_bytes: 99
      cum_lopri_usage_bytes: 0
      currently_lopri: false
    }
    children {
      flow { src_dc: "NY" dst_dc: "LA" job: "YT" host_id: 3 }
      predicted_demand_bps: 400
      ewma_usage_bps: 311
      cum_usage_bytes: 8900
      cum_hipri_usage_bytes: 8900
      cum_lopri_usage_bytes: 0
      currently_lopri: true
    }
  )");

  auto demand_view = HostLevelView::Create<FVSource::kPredictedDemand>(raw_info);

  EXPECT_THAT(demand_view.parent(), EqProto(raw_info.parent()));
  EXPECT_THAT(
      demand_view.children(),
      testing::ElementsAre(
          ChildFlowInfo{.child_id = 1, .volume_bps = 500, .currently_lopri = true},
          ChildFlowInfo{.child_id = 2, .volume_bps = 300, .currently_lopri = false},
          ChildFlowInfo{.child_id = 3, .volume_bps = 400, .currently_lopri = true}));

  auto usage_view = HostLevelView::Create<FVSource::kUsage>(raw_info);

  EXPECT_THAT(usage_view.parent(), EqProto(raw_info.parent()));
  EXPECT_THAT(
      usage_view.children(),
      testing::ElementsAre(
          ChildFlowInfo{.child_id = 1, .volume_bps = 500, .currently_lopri = true},
          ChildFlowInfo{.child_id = 2, .volume_bps = 300, .currently_lopri = false},
          ChildFlowInfo{.child_id = 3, .volume_bps = 311, .currently_lopri = true}));
}

TEST(JobLevelViewTest, Basic) {
  auto raw_info = ParseTextProto<proto::AggInfo>(R"(
    parent {
      flow { src_dc: "NY" dst_dc: "LA" }
      predicted_demand_bps: 1234
      ewma_usage_bps: 1111
      cum_usage_bytes: 9099
      cum_hipri_usage_bytes: 9000
      cum_lopri_usage_bytes: 99
      currently_lopri: false
    }
    children {
      flow { src_dc: "NY" dst_dc: "LA" job: "YT" host_id: 1 }
      predicted_demand_bps: 500
      ewma_usage_bps: 500
      cum_usage_bytes: 100
      cum_hipri_usage_bytes: 1
      cum_lopri_usage_bytes: 99
      currently_lopri: true
    }
    children {
      flow { src_dc: "NY" dst_dc: "LA" job: "FB" host_id: 2 }
      predicted_demand_bps: 300
      ewma_usage_bps: 300
      cum_usage_bytes: 99
      cum_hipri_usage_bytes: 99
      cum_lopri_usage_bytes: 0
      currently_lopri: false
    }
    children {
      flow { src_dc: "NY" dst_dc: "LA" job: "YT" host_id: 3 }
      predicted_demand_bps: 400
      ewma_usage_bps: 311
      cum_usage_bytes: 8900
      cum_hipri_usage_bytes: 8900
      cum_lopri_usage_bytes: 0
      currently_lopri: true
    }
  )");

  auto demand_view = JobLevelView::Create<FVSource::kPredictedDemand>(raw_info);

  EXPECT_THAT(demand_view.parent(), EqProto(raw_info.parent()));
  EXPECT_THAT(demand_view.job_index_of_host(), testing::ElementsAre(0, 1, 0));

  EXPECT_THAT(demand_view.children().size(), testing::Eq(2));
  EXPECT_THAT(demand_view.children()[0].volume_bps, testing::Eq(900));
  EXPECT_THAT(demand_view.children()[0].currently_lopri, testing::Eq(true));
  EXPECT_THAT(demand_view.children()[1].volume_bps, testing::Eq(300));
  EXPECT_THAT(demand_view.children()[1].currently_lopri, testing::Eq(false));
  EXPECT_THAT(demand_view.children()[0].child_id,
              testing::Ne(demand_view.children()[1].child_id));

  auto usage_view = JobLevelView::Create<FVSource::kUsage>(raw_info);

  EXPECT_THAT(usage_view.parent(), EqProto(raw_info.parent()));
  EXPECT_THAT(usage_view.job_index_of_host(), testing::ElementsAre(0, 1, 0));

  EXPECT_THAT(usage_view.children()[0].volume_bps, testing::Eq(811));
  EXPECT_THAT(usage_view.children()[0].currently_lopri, testing::Eq(true));
  EXPECT_THAT(usage_view.children()[1].volume_bps, testing::Eq(300));
  EXPECT_THAT(usage_view.children()[1].currently_lopri, testing::Eq(false));
  EXPECT_THAT(usage_view.children()[0].child_id,
              testing::Ne(usage_view.children()[1].child_id));

  EXPECT_EQ(usage_view.children()[0].child_id, demand_view.children()[0].child_id);
  EXPECT_EQ(usage_view.children()[1].child_id, demand_view.children()[1].child_id);
}

}  // namespace
}  // namespace heyp
