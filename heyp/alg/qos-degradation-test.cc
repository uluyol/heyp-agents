#include "heyp/alg/qos-degradation.h"

#include "absl/functional/bind_front.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/parse-text.h"

namespace heyp {
namespace {

proto::AggInfo ChildrenWithDemands(std::vector<int64_t> demands_bps) {
  proto::AggInfo info;
  for (int64_t d : demands_bps) {
    info.add_children()->set_predicted_demand_bps(d);
  }
  return info;
}

proto::AggInfo ChildrenWithDemandsAndPri(
    std::vector<std::pair<int64_t, bool>> demands_islopri_bps) {
  proto::AggInfo info;
  for (std::pair<int64_t, bool> p : demands_islopri_bps) {
    auto cp = info.add_children();
    cp->set_predicted_demand_bps(p.first);
    cp->set_currently_lopri(p.second);
  }
  return info;
}

template <bool StateToIncrease>
std::vector<bool> GreedyAssignToMinimizeGapWrapper(
    proto::AggInfo demands, const std::vector<bool> initial_lopri,
    std::vector<size_t> sorted_by_demand, int64_t cur_demand,
    int64_t want_demand) {
  std::vector<bool> lopri_children = initial_lopri;
  GreedyAssignToMinimizeGap<StateToIncrease>(
      {
          .cur_demand = cur_demand,
          .want_demand = want_demand,
          .children_sorted_by_dec_demand = sorted_by_demand,
          .agg_info = demands,
      },
      lopri_children);
  return lopri_children;
}

TEST(GreedyAssignToMinimizeGapTest, Directionality) {
  const proto::AggInfo demands = ChildrenWithDemands({200, 100, 300, 100});
  const std::vector<bool> initial_lopri = {true, false, false, true};
  const std::vector<size_t> sorted_by_demand{2, 0, 3, 1};

  auto assign_lopri =
      absl::bind_front(GreedyAssignToMinimizeGapWrapper<true>, demands,
                       initial_lopri, sorted_by_demand, 300);

  auto assign_hipri =
      absl::bind_front(GreedyAssignToMinimizeGapWrapper<false>, demands,
                       initial_lopri, sorted_by_demand, 400);

  constexpr bool t = true;
  constexpr bool f = false;

  EXPECT_THAT(assign_lopri(200), testing::ElementsAre(t, f, f, t));
  EXPECT_THAT(assign_lopri(400), testing::ElementsAre(t, t, f, t));
  EXPECT_THAT(assign_hipri(200), testing::ElementsAre(t, f, f, t));
  EXPECT_THAT(assign_hipri(600), testing::ElementsAre(f, f, f, t));
}

TEST(GreedyAssignToMinimizeGapTest, FlipCompletely) {
  const proto::AggInfo demands = ChildrenWithDemands({200, 100, 300, 100});
  const std::vector<bool> initial_lopri = {true, false, false, true};
  const std::vector<size_t> sorted_by_demand{2, 0, 3, 1};

  auto assign_lopri =
      absl::bind_front(GreedyAssignToMinimizeGapWrapper<true>, demands,
                       initial_lopri, sorted_by_demand, 300);

  auto assign_hipri =
      absl::bind_front(GreedyAssignToMinimizeGapWrapper<false>, demands,
                       initial_lopri, sorted_by_demand, 400);

  constexpr bool t = true;
  constexpr bool f = false;

  EXPECT_THAT(assign_lopri(700), testing::ElementsAre(t, t, t, t));
  EXPECT_THAT(assign_hipri(700), testing::ElementsAre(f, f, f, f));
}

TEST(HeypSigcomm20PickLOPRIChildrenTest, Directionality) {
  const proto::AggInfo info = ChildrenWithDemandsAndPri({
      {200, true},
      {100, false},
      {300, false},
      {100, true},
  });

  constexpr bool t = true;
  constexpr bool f = false;

  EXPECT_THAT(HeypSigcomm20PickLOPRIChildren(info, 0.28),
              testing::ElementsAre(t, f, f, f));
  EXPECT_THAT(HeypSigcomm20PickLOPRIChildren(info, 0.58),
              testing::ElementsAre(t, t, f, t));
  EXPECT_THAT(HeypSigcomm20PickLOPRIChildren(info, 0.71),
              testing::ElementsAre(t, t, f, t));
  EXPECT_THAT(HeypSigcomm20PickLOPRIChildren(info, 0.14),
              testing::ElementsAre(f, f, f, t));
}

TEST(HeypSigcomm20PickLOPRIChildrenTest, FlipCompletely) {
  const proto::AggInfo info = ChildrenWithDemandsAndPri({
      {200, true},
      {100, false},
      {300, false},
      {100, true},
  });

  constexpr bool t = true;
  constexpr bool f = false;

  EXPECT_THAT(HeypSigcomm20PickLOPRIChildren(info, 1),
              testing::ElementsAre(t, t, t, t));
  EXPECT_THAT(HeypSigcomm20PickLOPRIChildren(info, 0),
              testing::ElementsAre(f, f, f, f));
}

TEST(FracAdmittedAtLOPRITest, Basic) {
  EXPECT_EQ(FracAdmittedAtLOPRI(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 1000"),
                ParseTextProto<proto::FlowAlloc>(R"(
                  hipri_rate_limit_bps: 600
                  lopri_rate_limit_bps: 200
                )")),
            0.25);

  EXPECT_EQ(FracAdmittedAtLOPRI(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 640"),
                ParseTextProto<proto::FlowAlloc>(R"(
                  hipri_rate_limit_bps: 600
                  lopri_rate_limit_bps: 200
                )")),
            0.0625);

  EXPECT_EQ(FracAdmittedAtLOPRI(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 500"),
                ParseTextProto<proto::FlowAlloc>(R"(
                  hipri_rate_limit_bps: 600
                  lopri_rate_limit_bps: 200
                )")),
            0);

  EXPECT_EQ(FracAdmittedAtLOPRI(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 1000"),
                ParseTextProto<proto::FlowAlloc>(R"(
                  hipri_rate_limit_bps: 600
                  lopri_rate_limit_bps: 0
                )")),
            0);
  EXPECT_EQ(FracAdmittedAtLOPRI(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 1000"),
                ParseTextProto<proto::FlowAlloc>(R"(
                  hipri_rate_limit_bps: 0
                  lopri_rate_limit_bps: 900
                )")),
            1);
}

}  // namespace
}  // namespace heyp
