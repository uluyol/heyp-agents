#include "heyp/alg/fairness/nth-element.h"

#include <algorithm>
#include <vector>

#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

std::vector<int64_t> HeypAlgosNth(const std::vector<int64_t>& arg, int n) {
  std::vector<int64_t> result = arg;
  heyp::NthElement(result.begin(), result.begin() + n, result.end());
  return result;
}

std::vector<int64_t> StdNth(const std::vector<int64_t>& arg, int n) {
  std::vector<int64_t> result = arg;
  std::nth_element(result.begin(), result.begin() + n, result.end());
  return result;
}

class NthElementExhaustiveTest : public testing::TestWithParam<std::vector<int64_t>> {};

TEST_P(NthElementExhaustiveTest, MatchesStd) {
  for (int i = 0; i < GetParam().size(); i++) {
    SCOPED_TRACE(absl::Substitute("i = $0", i));
    auto result_routing_algos = HeypAlgosNth(GetParam(), i);
    auto result_std = StdNth(GetParam(), i);

    EXPECT_THAT(result_routing_algos[i], testing::Eq(result_std[i]));
    EXPECT_THAT(absl::MakeSpan(result_routing_algos).subspan(0, i),
                testing::Each(testing::Le(result_routing_algos[i])));
  }
}

struct SampledTest {
  std::vector<int64_t> data;
  std::vector<int> ns;
};

class NthElementSampledTest : public testing::TestWithParam<SampledTest> {};

TEST_P(NthElementSampledTest, MatchesStd) {
  for (int i : GetParam().ns) {
    auto result_routing_algos = HeypAlgosNth(GetParam().data, i);
    auto result_std = StdNth(GetParam().data, i);

    EXPECT_THAT(result_routing_algos[i], testing::Eq(result_std[i]));
    EXPECT_THAT(absl::MakeSpan(result_routing_algos).subspan(0, i),
                testing::Each(testing::Le(result_routing_algos[i])));
  }
}

std::vector<int64_t> RandomData(int n) {
  std::vector<int64_t> data(n, 0);
  for (int i = 0; i < n; i++) {
    data[i] = rand() % 55;
  }
  return data;
}

std::vector<int64_t> RepeatN(int n, int64_t v) { return std::vector<int64_t>(n, v); }

INSTANTIATE_TEST_SUITE_P(Sampled, NthElementExhaustiveTest,
                         testing::Values(RandomData(1), RandomData(10), RandomData(111),
                                         RandomData(301), RepeatN(257, 0),
                                         RepeatN(30, 4981)));

INSTANTIATE_TEST_SUITE_P(Sampled, NthElementSampledTest,
                         testing::Values(
                             SampledTest{
                                 RandomData(5000),
                                 {0, 1, 555, 1123, 4999},
                             },
                             SampledTest{
                                 RandomData(51003),
                                 {0, 1, 555, 3, 49945},
                             },
                             SampledTest{
                                 RepeatN(5000, 4),
                                 {0, 1, 555, 1123, 4999},
                             }));

}  // namespace
}  // namespace heyp