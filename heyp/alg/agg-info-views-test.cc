#include "heyp/alg/agg-info-views.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/parse-text.h"
#include "heyp/proto/testing.h"

namespace heyp {
namespace {

TEST(TransparentViewTest, Basic) {
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

  TransparentView view(raw_info);

  EXPECT_THAT(view.parent(), EqProto(raw_info.parent()));
  EXPECT_THAT(view.children(), EqRepeatedProto(raw_info.children()));
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

  google::protobuf::RepeatedPtrField<proto::FlowInfo> expected_job_children;
  expected_job_children.Add(ParseTextProto<proto::FlowInfo>(R"(
    flow { src_dc: "NY" dst_dc: "LA" job: "YT" }
    predicted_demand_bps: 900
    ewma_usage_bps: 811
    cum_usage_bytes: 9000
    cum_hipri_usage_bytes: 8901
    cum_lopri_usage_bytes: 99
    currently_lopri: true
  )"));
  expected_job_children.Add(ParseTextProto<proto::FlowInfo>(R"(
    flow { src_dc: "NY" dst_dc: "LA" job: "FB" }
    predicted_demand_bps: 300
    ewma_usage_bps: 300
    cum_usage_bytes: 99
    cum_hipri_usage_bytes: 99
    cum_lopri_usage_bytes: 0
    currently_lopri: false
  )"));

  JobLevelView view(raw_info);

  EXPECT_THAT(view.parent(), EqProto(raw_info.parent()));
  EXPECT_THAT(view.children(), EqRepeatedProto(expected_job_children));
  EXPECT_THAT(view.job_index_of_host(), testing::ElementsAre(0, 1, 0));
}

}  // namespace
}  // namespace heyp
