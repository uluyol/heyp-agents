#include "heyp/alg/sampler.h"

#include <cmath>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

struct ThresholdSamplerTestConfig {
  double approval = 0;
  double num_samples_at_approval = 0;
};

struct ThresholdSamplerTestRunResult {
  double got_num_samples = 0;
  double actual_usage = 0;
  double est_usage = 0;
};

std::ostream& operator<<(std::ostream& os, const ThresholdSamplerTestConfig& c) {
  return os << "{ approval = " << c.approval
            << ", num_samples_at_approval = " << c.num_samples_at_approval << "}";
}

TEST(ThresholdSamplerSimpleTest, Basic) {
  ThresholdSampler sampler(100, 100);
  int c = 0;
  absl::BitGen gen;
  for (int i = 0; i < 1000; i++) {
    if (sampler.ShouldInclude(gen, 0.5)) {
      c++;
    }
  }

  EXPECT_THAT(c, testing::AllOf(testing::Ge(450), testing::Le(550)));
}

class ThresholdSamplerTest : public testing::TestWithParam<ThresholdSamplerTestConfig> {};

TEST_P(ThresholdSamplerTest, AtApproval) {
  absl::BitGen gen;
  ThresholdSampler s(GetParam().num_samples_at_approval, GetParam().approval);

  // Run each config many times. We test that each individual run
  // has errors that are not too big, and that the mean error across
  // all runs is small.

  auto run_one_fn = [&]() -> ThresholdSamplerTestRunResult {
    constexpr static int kNumHosts = 1030;
    ThresholdSamplerTestRunResult run;
    auto est = s.NewAggUsageEstimator();
    for (int i = 0; i < kNumHosts; i++) {
      double usage = 2 * absl::Uniform<double>(absl::IntervalClosedOpen, gen, 0, 1) *
                     GetParam().approval / kNumHosts;
      run.actual_usage += usage;
      if (s.ShouldInclude(gen, usage)) {
        est.RecordSample(usage);
        run.got_num_samples++;
      }
    }
    if (GetParam().approval != 0) {
      EXPECT_NEAR(run.got_num_samples, GetParam().num_samples_at_approval,
                  GetParam().num_samples_at_approval * 0.5);
    }
    run.est_usage = est.EstUsage(kNumHosts);
    EXPECT_NEAR(run.est_usage, run.actual_usage, run.actual_usage * 0.5);
    return run;
  };

  double avg_num_samples = 0;
  double avg_usage_error = 0;
  constexpr static int kNumRuns = 100;
  for (int i = 0; i < kNumRuns; i++) {
    auto run = run_one_fn();
    avg_num_samples += run.got_num_samples;
    if (run.actual_usage == run.est_usage) {
      // want to add 0 to avg_usage_error, even in cases where actual usage = 0
      continue;
    }
    ASSERT_FALSE(std::isnan(run.actual_usage));
    ASSERT_FALSE(std::isnan(run.est_usage));
    ASSERT_NE(run.actual_usage, 0);
    avg_usage_error += (run.actual_usage - run.est_usage) / run.actual_usage;
  }

  avg_num_samples /= kNumRuns;
  avg_usage_error /= kNumRuns;

  if (GetParam().approval != 0) {
    EXPECT_NEAR(avg_num_samples, GetParam().num_samples_at_approval,
                GetParam().num_samples_at_approval * 0.05);
  }
  EXPECT_NEAR(avg_usage_error, 0, 0.05);
}

TEST_P(ThresholdSamplerTest, AboveApproval) {
  absl::BitGen gen;
  ThresholdSampler s(GetParam().num_samples_at_approval, GetParam().approval);

  // Run each config many times. We test that each individual run
  // has errors that are not too big, and that the mean error across
  // all runs is small.

  auto run_one_fn = [&]() -> ThresholdSamplerTestRunResult {
    constexpr static int kNumHosts = 1030;
    ThresholdSamplerTestRunResult run;
    auto est = s.NewAggUsageEstimator();
    for (int i = 0; i < kNumHosts; i++) {
      double usage = 4 * absl::Uniform<double>(absl::IntervalClosedOpen, gen, 0, 1) *
                     GetParam().approval / kNumHosts;
      run.actual_usage += usage;
      if (s.ShouldInclude(gen, usage)) {
        est.RecordSample(usage);
        run.got_num_samples++;
      }
    }
    if (GetParam().approval != 0) {
      EXPECT_GE(run.got_num_samples, 0.8 * GetParam().num_samples_at_approval);
    }
    run.est_usage = est.EstUsage(kNumHosts);
    EXPECT_NEAR(run.est_usage, run.actual_usage, run.actual_usage * 0.5);
    return run;
  };

  double avg_num_samples = 0;
  double avg_usage_error = 0;
  constexpr static int kNumRuns = 100;
  for (int i = 0; i < kNumRuns; i++) {
    auto run = run_one_fn();
    avg_num_samples += run.got_num_samples;
    if (run.actual_usage == run.est_usage) {
      // want to add 0 to avg_usage_error, even in cases where actual usage = 0
      continue;
    }
    ASSERT_FALSE(std::isnan(run.actual_usage));
    ASSERT_FALSE(std::isnan(run.est_usage));
    ASSERT_NE(run.actual_usage, 0);
  }

  avg_num_samples /= kNumRuns;
  avg_usage_error /= kNumRuns;

  if (GetParam().approval != 0) {
    EXPECT_GE(avg_num_samples, GetParam().num_samples_at_approval);
  }
  EXPECT_NEAR(avg_usage_error, 0, 0.05);
}

TEST_P(ThresholdSamplerTest, BelowApproval) {
  absl::BitGen gen;
  ThresholdSampler s(GetParam().num_samples_at_approval, GetParam().approval);

  auto run_one_fn = [&]() -> ThresholdSamplerTestRunResult {
    constexpr static int kNumHosts = 1030;
    ThresholdSamplerTestRunResult run;
    auto est = s.NewAggUsageEstimator();
    for (int i = 0; i < kNumHosts; i++) {
      double usage = absl::Uniform<double>(absl::IntervalClosedOpen, gen, 0, 1) *
                     GetParam().approval / kNumHosts;  // Usage is approx 0.5 * approval
      run.actual_usage += usage;
      if (s.ShouldInclude(gen, usage)) {
        est.RecordSample(usage);
        run.got_num_samples++;
      }
    }
    if (GetParam().approval != 0) {
      EXPECT_LE(run.got_num_samples, 1.2 * GetParam().num_samples_at_approval);
    }
    run.est_usage = est.EstUsage(kNumHosts);
    EXPECT_LE(run.est_usage, GetParam().approval);
    return run;
  };

  double avg_num_samples = 0;
  double avg_usage_error = 0;
  constexpr static int kNumRuns = 100;
  for (int i = 0; i < kNumRuns; i++) {
    auto run = run_one_fn();
    avg_num_samples += run.got_num_samples;
    if (run.actual_usage == run.est_usage) {
      // want to add 0 to avg_usage_error, even in cases where actual usage = 0
      continue;
    }
    ASSERT_FALSE(std::isnan(run.actual_usage));
    ASSERT_FALSE(std::isnan(run.est_usage));
    ASSERT_NE(run.actual_usage, 0);
  }

  avg_num_samples /= kNumRuns;
  avg_usage_error /= kNumRuns;

  if (GetParam().approval != 0) {
    EXPECT_LE(avg_num_samples, GetParam().num_samples_at_approval);
  }
  EXPECT_LE(avg_usage_error, 0.05);
}

INSTANTIATE_TEST_SUITE_P(BasicCases, ThresholdSamplerTest,
                         ::testing::Values(
                             ThresholdSamplerTestConfig{
                                 .approval = 0,
                                 .num_samples_at_approval = 101,
                             },
                             ThresholdSamplerTestConfig{
                                 .approval = 1,
                                 .num_samples_at_approval = 500,
                             },
                             ThresholdSamplerTestConfig{
                                 .approval = 3333,
                                 .num_samples_at_approval = 100,
                             },
                             ThresholdSamplerTestConfig{
                                 .approval = 7777,
                                 .num_samples_at_approval = 300,
                             }));

}  // namespace
}  // namespace heyp
