#include "heyp/alg/qos-downgrade.h"

#include "debug.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/alg/internal/hash-ring.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/parse-text.h"

namespace heyp {
namespace {

proto::AggInfo ChildrenWithDemands(std::vector<int64_t> demands_bps) {
  proto::AggInfo info;
  uint64_t num_demands = demands_bps.size();
  for (uint64_t i = 0; i < demands_bps.size(); ++i) {
    proto::FlowInfo* child = info.add_children();
    child->set_predicted_demand_bps(demands_bps[i]);
    child->mutable_flow()->set_host_id((internal::MaxId / num_demands) * i);
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
  uint64_t num_demands = demands_islopri_bps.size();
  for (uint64_t i = 0; i < demands_islopri_bps.size(); ++i) {
    const ChildInfo& p = demands_islopri_bps[i];
    auto child = info.add_children();
    child->mutable_flow()->set_job(p.job);
    child->mutable_flow()->set_host_id((internal::MaxId / num_demands) * i);
    child->set_predicted_demand_bps(p.demand_bps);
    child->set_currently_lopri(p.is_lopri);
  }
  return info;
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
  config.set_downgrade_usage(false);
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
  config.set_downgrade_usage(false);
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
  config.set_downgrade_usage(false);
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
  config.set_downgrade_usage(false);
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
  config.set_downgrade_usage(false);
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
  config.set_downgrade_usage(false);
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
  config.set_downgrade_usage(false);
  DowngradeSelector selector(config);

  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.428), testing::ElementsAre(f, f, f, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.429), testing::ElementsAre(t, t, f, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.571), testing::ElementsAre(t, t, f, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.572), testing::ElementsAre(f, f, t, t));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.999), testing::ElementsAre(f, f, t, t));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 1.000), testing::ElementsAre(t, t, t, t));
}

TEST(HashingLOPRIChildrenTest, Directionality) {
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
  config.set_type(proto::DS_HASHING);

  EXPECT_THAT(DowngradeSelector(config).PickLOPRIChildren(info, 0.28),
              testing::ElementsAre(t, t, f, f));
  EXPECT_THAT(DowngradeSelector(config).PickLOPRIChildren(info, 0.58),
              testing::ElementsAre(t, t, t, f));
  EXPECT_THAT(DowngradeSelector(config).PickLOPRIChildren(info, 0.71),
              testing::ElementsAre(t, t, t, f));
  EXPECT_THAT(DowngradeSelector(config).PickLOPRIChildren(info, 0.14),
              testing::ElementsAre(t, f, f, f));
}

TEST(HashingLOPRIChildrenTest, FlipCompletely) {
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
  config.set_type(proto::DS_HASHING);

  EXPECT_THAT(DowngradeSelector(config).PickLOPRIChildren(info, 1),
              testing::ElementsAre(t, t, t, t));
  EXPECT_THAT(DowngradeSelector(config).PickLOPRIChildren(info, 0),
              testing::ElementsAre(f, f, f, f));
}

TEST(HashingLOPRIChildrenTest, IsFIFO) {
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
  config.set_type(proto::DS_HASHING);
  DowngradeSelector selector(config);

  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.20), testing::ElementsAre(t, f, f, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.20), testing::ElementsAre(t, f, f, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.00), testing::ElementsAre(f, f, f, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.50), testing::ElementsAre(f, t, t, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.25), testing::ElementsAre(f, f, t, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.00), testing::ElementsAre(f, f, f, f));
  EXPECT_THAT(selector.PickLOPRIChildren(info, 0.50), testing::ElementsAre(t, f, f, t));
}

TEST(FracAdmittedAtLOPRITest, Basic) {
  EXPECT_EQ(FracAdmittedAtLOPRI<FVSource::kPredictedDemand>(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 1000"), 600, 200),
            0.25);

  EXPECT_EQ(FracAdmittedAtLOPRI<FVSource::kPredictedDemand>(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 640"), 600, 200),
            0.0625);

  EXPECT_EQ(FracAdmittedAtLOPRI<FVSource::kPredictedDemand>(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 500"), 600, 200),
            0);
}

TEST(FracAdmittedAtLOPRITest, AllLOPRI) {
  EXPECT_EQ(FracAdmittedAtLOPRI<FVSource::kUsage>(
                ParseTextProto<proto::FlowInfo>("ewma_usage_bps: 1000"), 0, 900),
            1);
}

TEST(FracAdmittedAtLOPRITest, AllHIPRI) {
  EXPECT_EQ(FracAdmittedAtLOPRI<FVSource::kUsage>(
                ParseTextProto<proto::FlowInfo>("ewma_usage_bps: 1000"), 600, 0),
            0);
}

TEST(FracAdmittedAtLOPRITest, ZeroLimit) {
  EXPECT_EQ(FracAdmittedAtLOPRI<FVSource::kPredictedDemand>(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 1000"), 0, 0),
            0);
}

TEST(FracAdmittedAtLOPRITest, ZeroDemand) {
  EXPECT_EQ(FracAdmittedAtLOPRI<FVSource::kPredictedDemand>(
                ParseTextProto<proto::FlowInfo>("predicted_demand_bps: 0"), 600, 0),
            0);
}

TEST(ShouldProbeLOPRITest, Basic) {
  proto::AggInfo info = ChildrenWithDemands({1000, 800, 600, 400, 200, 100});
  info.mutable_parent()->set_predicted_demand_bps(2499);
  auto logger = MakeLogger("test");

  ASSERT_EQ(FracAdmittedAtLOPRIToProbe<FVSource::kPredictedDemand>(info, 2500, 600, 1.9,
                                                                   -1, &logger),
            -1);

  info.mutable_parent()->set_predicted_demand_bps(2500);
  ASSERT_NEAR(FracAdmittedAtLOPRIToProbe<FVSource::kPredictedDemand>(info, 2500, 600, 1.9,
                                                                     -1, &logger),
              0.04, 0.00001);

  info.mutable_parent()->set_predicted_demand_bps(3000);
  ASSERT_NEAR(FracAdmittedAtLOPRIToProbe<FVSource::kPredictedDemand>(info, 2500, 600, 1.9,
                                                                     0.2, &logger),
              0.2, 0.00001);

  info.mutable_parent()->set_predicted_demand_bps(3000);
  ASSERT_NEAR(FracAdmittedAtLOPRIToProbe<FVSource::kPredictedDemand>(info, 2500, 600, 1.2,
                                                                     0.2, &logger),
              0.2, 0.00001);

  ASSERT_EQ(FracAdmittedAtLOPRIToProbe<FVSource::kPredictedDemand>(info, 2500, 0, 1.9, 0,
                                                                   &logger),
            0);
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
