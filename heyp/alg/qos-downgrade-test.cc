#include "heyp/alg/qos-downgrade.h"

#include "absl/functional/bind_front.h"
#include "debug.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/config.pb.h"
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

struct ChildInfo {
  int64_t demand_bps = 0;
  bool is_lopri = false;
  std::string job = "";
};

proto::AggInfo ChildrenWithDemandsAndPri(std::vector<ChildInfo> demands_islopri_bps) {
  proto::AggInfo info;
  for (const ChildInfo& p : demands_islopri_bps) {
    auto cp = info.add_children();
    cp->mutable_flow()->set_job(p.job);
    cp->set_predicted_demand_bps(p.demand_bps);
    cp->set_currently_lopri(p.is_lopri);
  }
  return info;
}

template <bool StateToIncrease>
std::vector<bool> GreedyAssignToMinimizeGapWrapper(proto::AggInfo demands,
                                                   const std::vector<bool> initial_lopri,
                                                   std::vector<size_t> sorted_by_demand,
                                                   int64_t cur_demand,
                                                   int64_t want_demand) {
  std::vector<bool> lopri_children = initial_lopri;
  GreedyAssignToMinimizeGap<StateToIncrease>(
      {
          .cur_demand = cur_demand,
          .want_demand = want_demand,
          .children_sorted_by_dec_demand = sorted_by_demand,
          .agg_info = TransparentView(demands),
      },
      lopri_children, false);
  return lopri_children;
}

TEST(GreedyAssignToMinimizeGapTest, Directionality) {
  const proto::AggInfo demands = ChildrenWithDemands({200, 100, 300, 100});
  const std::vector<bool> initial_lopri = {true, false, false, true};
  const std::vector<size_t> sorted_by_demand{2, 0, 3, 1};

  auto assign_lopri = absl::bind_front(GreedyAssignToMinimizeGapWrapper<true>, demands,
                                       initial_lopri, sorted_by_demand, 300);

  auto assign_hipri = absl::bind_front(GreedyAssignToMinimizeGapWrapper<false>, demands,
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

  auto assign_lopri = absl::bind_front(GreedyAssignToMinimizeGapWrapper<true>, demands,
                                       initial_lopri, sorted_by_demand, 300);

  auto assign_hipri = absl::bind_front(GreedyAssignToMinimizeGapWrapper<false>, demands,
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

  proto::DowngradeSelector config;
  config.set_type(proto::DS_HEYP_SIGCOMM20);
  DowngradeSelector selector(config);

  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.28), testing::ElementsAre(t, f, f, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.58), testing::ElementsAre(t, t, f, t));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.71), testing::ElementsAre(t, t, f, t));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.14), testing::ElementsAre(f, f, f, t));
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

  auto logger = MakeLogger("test");
  proto::DowngradeSelector config;
  config.set_type(proto::DS_HEYP_SIGCOMM20);
  DowngradeSelector selector(config);

  EXPECT_THAT(selector.PickLOPRIChildren(info, 1), testing::ElementsAre(t, t, t, t));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0), testing::ElementsAre(f, f, f, f));
}

TEST(LargestFirstPickLOPRIChildrenTest, Directionality) {
  const proto::AggInfo info = ChildrenWithDemandsAndPri({
      {200, true},
      {100, false},
      {300, false},
      {100, true},
  });

  constexpr bool t = true;
  constexpr bool f = false;

  auto logger = MakeLogger("test");

  proto::DowngradeSelector config;
  config.set_type(proto::DS_LARGEST_FIRST);
  DowngradeSelector selector(config);

  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.28), testing::ElementsAre(f, f, t, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.58), testing::ElementsAre(t, f, t, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.71), testing::ElementsAre(t, f, t, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.14), testing::ElementsAre(f, f, f, f));
}

TEST(LargestFirstPickLOPRIChildrenTest, FlipCompletely) {
  const proto::AggInfo info = ChildrenWithDemandsAndPri({
      {200, true},
      {100, false},
      {300, false},
      {100, true},
  });

  constexpr bool t = true;
  constexpr bool f = false;

  auto logger = MakeLogger("test");

  proto::DowngradeSelector config;
  config.set_type(proto::DS_LARGEST_FIRST);
  DowngradeSelector selector(config);

  EXPECT_THAT(selector.PickLOPRIChildren(info, 1), testing::ElementsAre(t, t, t, t));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0), testing::ElementsAre(f, f, f, f));
}

TEST(KnapsackSolverPickLOPRIChildrenTest, Directionality) {
  const proto::AggInfo info = ChildrenWithDemandsAndPri({
      {200, true},
      {100, false},
      {300, false},
      {100, true},
  });

  constexpr bool t = true;
  constexpr bool f = false;

  auto logger = MakeLogger("test");

  proto::DowngradeSelector config;
  config.set_type(proto::DS_KNAPSACK_SOLVER);
  DowngradeSelector selector(config);

  EXPECT_THAT(
      selector.PickLOPRIChildren(info, 0.28),
      testing::AnyOf(testing::ElementsAre(f, t, f, f), testing::ElementsAre(f, f, f, t)));
  EXPECT_THAT(
      selector.PickLOPRIChildren(info, 0.58),
      testing::AnyOf(testing::ElementsAre(t, t, f, t), testing::ElementsAre(f, t, t, f),
                     testing::ElementsAre(f, f, t, t)));
  EXPECT_THAT(
      selector.PickLOPRIChildren(info, 0.71),
      testing::AnyOf(testing::ElementsAre(t, t, f, t), testing::ElementsAre(f, t, t, f),
                     testing::ElementsAre(f, f, t, t)));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.14), testing::ElementsAre(f, f, f, f));
}

TEST(KnapsackSolverPickLOPRIChildrenTest, FlipCompletely) {
  const proto::AggInfo info = ChildrenWithDemandsAndPri({
      {200, true},
      {100, false},
      {300, false},
      {100, true},
  });

  constexpr bool t = true;
  constexpr bool f = false;

  auto logger = MakeLogger("test");

  proto::DowngradeSelector config;
  config.set_type(proto::DS_KNAPSACK_SOLVER);
  DowngradeSelector selector(config);

  EXPECT_THAT(selector.PickLOPRIChildren(info, 1), testing::ElementsAre(t, t, t, t));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0), testing::ElementsAre(f, f, f, f));
}

TEST(KnapsackSolverPickLOPRIChildrenTest, JobLevel) {
  const proto::AggInfo info = ChildrenWithDemandsAndPri({
      {200, true, "YT"},
      {100, false, "YT"},
      {300, false, "FB"},
      {100, true, "FB"},
  });

  constexpr bool t = true;
  constexpr bool f = false;

  auto logger = MakeLogger("test");

  proto::DowngradeSelector config;
  config.set_type(proto::DS_KNAPSACK_SOLVER);
  config.set_downgrade_jobs(true);
  DowngradeSelector selector(config);

  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.428), testing::ElementsAre(f, f, f, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.429), testing::ElementsAre(t, t, f, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.571), testing::ElementsAre(t, t, f, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.572), testing::ElementsAre(f, f, t, t));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.999), testing::ElementsAre(f, f, t, t));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 1.000), testing::ElementsAre(t, t, t, t));
}

TEST(FracAdmittedAtLOPRITest, Basic) {
  EXPECT_EQ(FracAdmittedAtLOPRI(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 1000"), 600, 200),
            0.25);

  EXPECT_EQ(FracAdmittedAtLOPRI(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 640"), 600, 200),
            0.0625);

  EXPECT_EQ(FracAdmittedAtLOPRI(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 500"), 600, 200),
            0);
}

TEST(FracAdmittedAtLOPRITest, AllLOPRI) {
  EXPECT_EQ(FracAdmittedAtLOPRI(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 1000"), 0, 900),
            1);
}

TEST(FracAdmittedAtLOPRITest, AllHIPRI) {
  EXPECT_EQ(FracAdmittedAtLOPRI(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 1000"), 600, 0),
            0);
}

TEST(FracAdmittedAtLOPRITest, ZeroLimit) {
  EXPECT_EQ(FracAdmittedAtLOPRI(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 1000"), 0, 0),
            0);
}

TEST(FracAdmittedAtLOPRITest, ZeroDemand) {
  EXPECT_EQ(FracAdmittedAtLOPRI(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 0"), 600, 0),
            0);
}

TEST(ShouldProbeLOPRITest, Basic) {
  proto::AggInfo info = ChildrenWithDemands({1000, 800, 600, 400, 200, 100});
  info.mutable_parent()->set_predicted_demand_bps(2499);
  auto logger = MakeLogger("test");

  ASSERT_EQ(FracAdmittedAtLOPRIToProbe(info, 2500, 600, 1.9, -1, &logger), -1);

  info.mutable_parent()->set_predicted_demand_bps(2500);
  ASSERT_NEAR(FracAdmittedAtLOPRIToProbe(info, 2500, 600, 1.9, -1, &logger), 0.04,
              0.00001);

  info.mutable_parent()->set_predicted_demand_bps(3000);
  ASSERT_NEAR(FracAdmittedAtLOPRIToProbe(info, 2500, 600, 1.9, 0.2, &logger), 0.2,
              0.00001);

  info.mutable_parent()->set_predicted_demand_bps(3000);
  ASSERT_NEAR(FracAdmittedAtLOPRIToProbe(info, 2500, 600, 1.2, 0.2, &logger), 0.2,
              0.00001);

  ASSERT_EQ(FracAdmittedAtLOPRIToProbe(info, 2500, 0, 1.9, 0, &logger), 0);
}

class HeypSigcomm20MaybeReviseLOPRIAdmissionTest : public testing::Test {
 protected:
  struct State {
    proto::FlowAlloc alloc;
    double frac_lopri = 0;
    absl::Time last_time = absl::UnixEpoch();
    int64_t last_cum_hipri_usage_bytes = 0;
    int64_t last_cum_lopri_usage_bytes = 0;
  };

  static State NewState(double frac_lopri, int64_t hipri_limit_bps,
                        int64_t lopri_limit_bps) {
    State st;
    st.alloc.set_hipri_rate_limit_bps(hipri_limit_bps);
    st.alloc.set_lopri_rate_limit_bps(lopri_limit_bps);
    st.frac_lopri = frac_lopri;
    return st;
  }

  static absl::Time T(int64_t sec) { return absl::UnixEpoch() + absl::Seconds(sec); }

  static proto::FlowInfo NewInfo(int64_t cum_hipri_usage_bytes,
                                 int64_t cum_lopri_usage_bytes) {
    proto::FlowInfo info;
    info.mutable_flow()->set_src_dc("x");
    info.set_cum_hipri_usage_bytes(cum_hipri_usage_bytes);
    info.set_cum_lopri_usage_bytes(cum_lopri_usage_bytes);
    return info;
  }
};

TEST_F(HeypSigcomm20MaybeReviseLOPRIAdmissionTest, Basic) {
  auto logger = MakeLogger("test-basic");
  EXPECT_EQ(HeypSigcomm20MaybeReviseLOPRIAdmission(1.0, T(1), NewInfo(900, 300),
                                                   NewState(0.25, 7200, 7200), &logger),
            7200);
  EXPECT_EQ(HeypSigcomm20MaybeReviseLOPRIAdmission(0.9, T(1), NewInfo(900, 271),
                                                   NewState(0.25, 7200, 7200), &logger),
            7200);
  EXPECT_EQ(HeypSigcomm20MaybeReviseLOPRIAdmission(0.9, T(1), NewInfo(900, 269),
                                                   NewState(0.25, 7200, 7200), &logger),
            2152);
}

TEST_F(HeypSigcomm20MaybeReviseLOPRIAdmissionTest, AllLOPRI) {
  auto logger = MakeLogger("test-all-lopri");
  EXPECT_EQ(HeypSigcomm20MaybeReviseLOPRIAdmission(1.0, T(1), NewInfo(10, 500),
                                                   NewState(1.0, 0, 7200), &logger),
            7200);
}

TEST_F(HeypSigcomm20MaybeReviseLOPRIAdmissionTest, AllHIPRI) {
  auto logger = MakeLogger("test-all-hipri");
  EXPECT_EQ(HeypSigcomm20MaybeReviseLOPRIAdmission(1.0, T(1), NewInfo(900, 10),
                                                   NewState(0.0, 7200, 0), &logger),
            0);
}

TEST_F(HeypSigcomm20MaybeReviseLOPRIAdmissionTest, ZeroUsage) {
  auto logger = MakeLogger("zero-usage");
  EXPECT_EQ(HeypSigcomm20MaybeReviseLOPRIAdmission(1.0, T(1), NewInfo(0, 0),
                                                   NewState(1.0, 7200, 7200), &logger),
            7200);
}

TEST_F(HeypSigcomm20MaybeReviseLOPRIAdmissionTest, ZeroLimit) {
  auto logger = MakeLogger("zero-limit");
  EXPECT_EQ(HeypSigcomm20MaybeReviseLOPRIAdmission(1.0, T(1), NewInfo(10, 500),
                                                   NewState(0.0, 0, 0), &logger),
            0);
}

TEST_F(HeypSigcomm20MaybeReviseLOPRIAdmissionTest, AllHIPRIFailed) {
  auto logger = MakeLogger("all-hipri-failed");
  EXPECT_EQ(HeypSigcomm20MaybeReviseLOPRIAdmission(1.0, T(1), NewInfo(0, 300),
                                                   NewState(0.0, 7200, 7200), &logger),
            7200);
}

TEST_F(HeypSigcomm20MaybeReviseLOPRIAdmissionTest, AllLOPRIFailed) {
  auto logger = MakeLogger("all-lopri-failed");
  EXPECT_EQ(HeypSigcomm20MaybeReviseLOPRIAdmission(0.5, T(1), NewInfo(900, 0),
                                                   NewState(0.5, 7200, 7200), &logger),
            0);
}

}  // namespace
}  // namespace heyp
