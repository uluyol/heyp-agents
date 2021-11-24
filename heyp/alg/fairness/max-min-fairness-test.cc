#include "heyp/alg/fairness/max-min-fairness.h"

#include "absl/algorithm/container.h"
#include "absl/strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

using ::testing::Eq;
using ::testing::SizeIs;

class SingleLinkMaxMinFairnessProblemTest
    : public testing::TestWithParam<SingleLinkMaxMinFairnessProblemOptions> {
 public:
  SingleLinkMaxMinFairnessProblemTest() : problem_(GetParam()) {}

 protected:
  SingleLinkMaxMinFairnessProblem problem_;
  std::vector<int64_t> result_;
};

TEST_P(SingleLinkMaxMinFairnessProblemTest, NoRequests) {
  int64_t waterlevel = problem_.ComputeWaterlevel(0, {});
  problem_.SetAllocations(waterlevel, {}, &result_);
  EXPECT_THAT(waterlevel, Eq(0));
  EXPECT_THAT(result_, SizeIs(0));

  waterlevel = problem_.ComputeWaterlevel(100, {});
  problem_.SetAllocations(waterlevel, {}, &result_);
  EXPECT_THAT(waterlevel, Eq(0));
  EXPECT_THAT(result_, SizeIs(0));
}

TEST_P(SingleLinkMaxMinFairnessProblemTest, AllZero) {
  std::vector<std::vector<int64_t>> all_demands{
      {0, 0, 0},
      {0},
      {0, 0},
  };

  for (std::vector<int64_t> demands : all_demands) {
    int64_t waterlevel = problem_.ComputeWaterlevel(0, demands);
    problem_.SetAllocations(waterlevel, demands, &result_);
    EXPECT_THAT(waterlevel, Eq(0));
    EXPECT_THAT(result_, Eq(demands));
  }
}

std::vector<std::vector<int64_t>> BasicDemands() {
  return {
      {1, 4, 5, 1, 2, 88, 1912},
      {3, 3, 9},
      {999999999, 2413541, 2351},
      {1, 2, 4, 8, 16, 64, 32, 256, 128, 2048, 512, 1024},
      {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37},
  };
}

TEST_P(SingleLinkMaxMinFairnessProblemTest, AllSatisfied) {
  for (std::vector<int64_t> demands : BasicDemands()) {
    int64_t capacity = absl::c_accumulate(demands, 0);
    const int64_t max_demand = *absl::c_max_element(demands);
    int64_t waterlevel = problem_.ComputeWaterlevel(capacity, demands);
    problem_.SetAllocations(waterlevel, demands, &result_);
    EXPECT_THAT(waterlevel, Eq(max_demand));
    EXPECT_THAT(result_, Eq(demands));
  }
}

TEST_P(SingleLinkMaxMinFairnessProblemTest, AllVerySatisfied) {
  for (std::vector<int64_t> demands : BasicDemands()) {
    int64_t capacity = absl::c_accumulate(demands, 0LL);
    const int64_t max_demand = *absl::c_max_element(demands);
    capacity = 13 * capacity + 10;
    int64_t waterlevel = problem_.ComputeWaterlevel(capacity, demands);
    problem_.SetAllocations(waterlevel, demands, &result_);
    EXPECT_THAT(waterlevel, Eq(max_demand));
    EXPECT_THAT(result_, Eq(demands));
  }
}

TEST_P(SingleLinkMaxMinFairnessProblemTest, BiggestNotSatisfied) {
  for (std::vector<int64_t> demands : BasicDemands()) {
    std::vector<int64_t> expected = demands;
    int64_t max = *absl::c_max_element(demands);
    int64_t second_max = std::numeric_limits<int64_t>::min();
    int64_t capacity = 0;
    for (int64_t v : demands) {
      if (v < max) {
        second_max = std::max(v, second_max);
        capacity += v;
      }
    }
    for (size_t i = 0; i < demands.size(); i++) {
      if (demands[i] == max) {
        capacity += second_max;
        expected[i] = second_max;
      }
    }
    SCOPED_TRACE(absl::Substitute("capacity: $0", capacity));
    int64_t waterlevel = problem_.ComputeWaterlevel(capacity, demands);
    problem_.SetAllocations(waterlevel, demands, &result_);
    EXPECT_THAT(waterlevel, Eq(second_max));
    EXPECT_THAT(result_, Eq(expected));
  }
}

TEST_P(SingleLinkMaxMinFairnessProblemTest, NoneSatisfied) {
  const std::vector<int64_t> demands{2, 5, 7};
  int64_t waterlevel = problem_.ComputeWaterlevel(5, demands);
  problem_.SetAllocations(waterlevel, demands, &result_);
  EXPECT_THAT(waterlevel, Eq(1));
  EXPECT_THAT(result_, Eq(std::vector<int64_t>{1, 1, 1}));
}

TEST_P(SingleLinkMaxMinFairnessProblemTest, HalfSatisfied) {
  const std::vector<int64_t> demands{7, 20, 23, 51, 299};
  int64_t waterlevel = problem_.ComputeWaterlevel(100, demands);
  problem_.SetAllocations(waterlevel, demands, &result_);
  EXPECT_THAT(waterlevel, Eq(25));
  EXPECT_THAT(result_, Eq(std::vector<int64_t>{7, 20, 23, 25, 25}));
}

INSTANTIATE_TEST_SUITE_P(
    AllSolvingMethods, SingleLinkMaxMinFairnessProblemTest,
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
