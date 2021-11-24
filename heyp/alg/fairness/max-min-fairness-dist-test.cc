#include "heyp/alg/fairness/max-min-fairness-dist.h"

#include "absl/algorithm/container.h"
#include "absl/strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

using ::testing::Eq;
using ::testing::SizeIs;

std::vector<ValCount> FlowDemands(const std::vector<double>& demands) {
  std::vector<ValCount> ret;
  ret.reserve(demands.size());
  for (double d : demands) {
    ret.push_back(ValCount{.val = d, .expected_count = 1});
  }
  return ret;
}

class SingleLinkMaxMinFairnessDistProblemTest
    : public testing::TestWithParam<SingleLinkMaxMinFairnessProblemOptions> {
 public:
  SingleLinkMaxMinFairnessDistProblemTest() : problem_(GetParam()) {}

 protected:
  SingleLinkMaxMinFairnessDistProblem problem_;
};

TEST_P(SingleLinkMaxMinFairnessDistProblemTest, NoRequests) {
  double waterlevel = problem_.ComputeWaterlevel(0, {});
  EXPECT_THAT(waterlevel, Eq(0));

  waterlevel = problem_.ComputeWaterlevel(100, {});
  EXPECT_THAT(waterlevel, Eq(0));
}

TEST_P(SingleLinkMaxMinFairnessDistProblemTest, AllZero) {
  std::vector<std::vector<double>> all_demands{
      {0, 0, 0},
      {0},
      {0, 0},
  };

  for (std::vector<double> demands : all_demands) {
    double waterlevel = problem_.ComputeWaterlevel(0, FlowDemands(demands));
    EXPECT_THAT(waterlevel, Eq(0));
  }
}

std::vector<std::vector<double>> BasicDemands() {
  return {
      {1, 4, 5, 1, 2, 88, 1912},
      {3, 3, 9},
      {999999999, 2413541, 2351},
      {1, 2, 4, 8, 16, 64, 32, 256, 128, 2048, 512, 1024},
      {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37},
  };
}

TEST_P(SingleLinkMaxMinFairnessDistProblemTest, AllSatisfied) {
  for (std::vector<double> demands : BasicDemands()) {
    double capacity = absl::c_accumulate(demands, 0);
    const double max_demand = *absl::c_max_element(demands);
    double waterlevel = problem_.ComputeWaterlevel(capacity, FlowDemands(demands));
    EXPECT_THAT(waterlevel, Eq(max_demand));
  }
}

TEST_P(SingleLinkMaxMinFairnessDistProblemTest, AllVerySatisfied) {
  for (std::vector<double> demands : BasicDemands()) {
    double capacity = absl::c_accumulate(demands, 0LL);
    const double max_demand = *absl::c_max_element(demands);
    capacity = 13 * capacity + 10;
    double waterlevel = problem_.ComputeWaterlevel(capacity, FlowDemands(demands));
    EXPECT_THAT(waterlevel, Eq(max_demand));
  }
}

TEST_P(SingleLinkMaxMinFairnessDistProblemTest, BiggestNotSatisfied) {
  for (std::vector<double> demands : BasicDemands()) {
    double max = *absl::c_max_element(demands);
    double second_max = std::numeric_limits<int32_t>::min();
    double capacity = 0;
    for (double v : demands) {
      if (v < max) {
        second_max = std::max<double>(v, second_max);
        capacity += v;
      }
    }
    for (size_t i = 0; i < demands.size(); i++) {
      if (demands[i] == max) {
        capacity += second_max;
      }
    }
    SCOPED_TRACE(absl::Substitute("capacity: $0", capacity));
    double waterlevel = problem_.ComputeWaterlevel(capacity, FlowDemands(demands));
    EXPECT_THAT(waterlevel, Eq(second_max));
  }
}

TEST_P(SingleLinkMaxMinFairnessDistProblemTest, NoneSatisfied) {
  const std::vector<double> demands{2, 5, 7};
  double waterlevel = problem_.ComputeWaterlevel(5, FlowDemands(demands));
  EXPECT_THAT(waterlevel, Eq(5.0 / 3.0));
}

TEST_P(SingleLinkMaxMinFairnessDistProblemTest, HalfSatisfied) {
  const std::vector<double> demands{7, 20, 23, 51, 299};
  double waterlevel = problem_.ComputeWaterlevel(100, FlowDemands(demands));
  EXPECT_THAT(waterlevel, Eq(25));
}

TEST_P(SingleLinkMaxMinFairnessDistProblemTest, AllSatisfied_Frac) {
  const std::vector<ValCount> demands{{10, 1.5}, {20, 1}};
  double waterlevel = problem_.ComputeWaterlevel(35, demands);
  EXPECT_THAT(waterlevel, Eq(20));
}

TEST_P(SingleLinkMaxMinFairnessDistProblemTest, BarelyUnsatisfiedSatisfied_Frac) {
  const std::vector<ValCount> demands{{10, 1.5}, {20, 1}};
  double waterlevel = problem_.ComputeWaterlevel(34, demands);
  EXPECT_THAT(waterlevel, Eq(19));
}

TEST_P(SingleLinkMaxMinFairnessDistProblemTest, AllVerySatisfied_Frac) {
  const std::vector<ValCount> demands{{10, 1.5}, {20, 1}};
  double waterlevel = problem_.ComputeWaterlevel(100, demands);
  EXPECT_THAT(waterlevel, Eq(20));
}

INSTANTIATE_TEST_SUITE_P(
    AllSolvingMethods, SingleLinkMaxMinFairnessDistProblemTest,
    testing::ValuesIn(std::vector<SingleLinkMaxMinFairnessProblemOptions>{
        /* Full Sort */
        {
            .solve_method = SingleLinkMaxMinFairnessProblemOptions::kFullSort,
            .enable_tiny_flow_opt = false,
        },
        {
            .solve_method = SingleLinkMaxMinFairnessProblemOptions::kFullSort,
            .enable_tiny_flow_opt = true,
        },
        /* Partial Sort */
        {
            .solve_method = SingleLinkMaxMinFairnessProblemOptions::kPartialSort,
            .enable_tiny_flow_opt = false,
        },
        {
            .solve_method = SingleLinkMaxMinFairnessProblemOptions::kPartialSort,
            .enable_tiny_flow_opt = true,
        },
    }));

}  // namespace
}  // namespace heyp
