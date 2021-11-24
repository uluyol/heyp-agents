#include <stdint.h>

#include <random>

#include "absl/functional/function_ref.h"
#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "heyp/alg/fairness/max-min-fairness-dist.h"

namespace heyp {
namespace {

static void BenchSingleLinkMaxMinFairnessDistProblem(
    benchmark::State& state, absl::FunctionRef<double(double)> total_to_capacity_fn,
    SingleLinkMaxMinFairnessDistProblem* problem) {
  std::vector<ValCount> demands(state.range(0), ValCount{});
  std::mt19937_64 rng(0);
  double total = 0;
  for (size_t i = 0; i < demands.size(); i++) {
    double d = absl::Uniform<double>(rng, 0, 1000);
    total += d;
    demands[i] = ValCount{.val = d, .expected_count = 1};
  }
  const double capacity = total_to_capacity_fn(total);
  double waterlevel = 0;
  for (auto _ : state) {
    waterlevel = problem->ComputeWaterlevel(capacity, demands);
  }
  benchmark::DoNotOptimize(waterlevel);
}

// With Tiny Flow Optimization //

static void BM_SingleLinkMaxMinFairnessDistProblem_WithTinyOpt_Demand_Eq_DoubleLimit(
    benchmark::State& state) {
  SingleLinkMaxMinFairnessDistProblem problem({.enable_tiny_flow_opt = true});
  BenchSingleLinkMaxMinFairnessDistProblem(
      state, [](double total) { return total / 2; }, &problem);
}

BENCHMARK(BM_SingleLinkMaxMinFairnessDistProblem_WithTinyOpt_Demand_Eq_DoubleLimit)
    ->RangeMultiplier(10)
    ->Range(10, 10000);

static void BM_SingleLinkMaxMinFairnessDistProblem_WithTinyOpt_Demand_Eq_Limit(
    benchmark::State& state) {
  SingleLinkMaxMinFairnessDistProblem problem({.enable_tiny_flow_opt = true});
  BenchSingleLinkMaxMinFairnessDistProblem(
      state, [](double total) { return total; }, &problem);
}

BENCHMARK(BM_SingleLinkMaxMinFairnessDistProblem_WithTinyOpt_Demand_Eq_Limit)
    ->RangeMultiplier(10)
    ->Range(10, 10000);

static void BM_SingleLinkMaxMinFairnessDistProblem_WithTinyOpt_Demand_Eq_HalfLimit(
    benchmark::State& state) {
  SingleLinkMaxMinFairnessDistProblem problem({.enable_tiny_flow_opt = true});
  BenchSingleLinkMaxMinFairnessDistProblem(
      state, [](double total) { return total * 2; }, &problem);
}

BENCHMARK(BM_SingleLinkMaxMinFairnessDistProblem_WithTinyOpt_Demand_Eq_HalfLimit)
    ->RangeMultiplier(10)
    ->Range(10, 10000);

// Without Tiny Flow Optimization //

static void BM_SingleLinkMaxMinFairnessDistProblem_NoTinyOpt_Demand_Eq_DoubleLimit(
    benchmark::State& state) {
  SingleLinkMaxMinFairnessDistProblem problem({.enable_tiny_flow_opt = false});
  BenchSingleLinkMaxMinFairnessDistProblem(
      state, [](double total) { return total / 2; }, &problem);
}

BENCHMARK(BM_SingleLinkMaxMinFairnessDistProblem_NoTinyOpt_Demand_Eq_DoubleLimit)
    ->RangeMultiplier(10)
    ->Range(10, 10000);

static void BM_SingleLinkMaxMinFairnessDistProblem_NoTinyOpt_Demand_Eq_Limit(
    benchmark::State& state) {
  SingleLinkMaxMinFairnessDistProblem problem({.enable_tiny_flow_opt = false});
  BenchSingleLinkMaxMinFairnessDistProblem(
      state, [](double total) { return total; }, &problem);
}

BENCHMARK(BM_SingleLinkMaxMinFairnessDistProblem_NoTinyOpt_Demand_Eq_Limit)
    ->RangeMultiplier(10)
    ->Range(10, 10000);

static void BM_SingleLinkMaxMinFairnessDistProblem_NoTinyOpt_Demand_Eq_HalfLimit(
    benchmark::State& state) {
  SingleLinkMaxMinFairnessDistProblem problem({.enable_tiny_flow_opt = false});
  BenchSingleLinkMaxMinFairnessDistProblem(
      state, [](double total) { return total * 2; }, &problem);
}

BENCHMARK(BM_SingleLinkMaxMinFairnessDistProblem_NoTinyOpt_Demand_Eq_HalfLimit)
    ->RangeMultiplier(10)
    ->Range(10, 10000);

}  // namespace
}  // namespace heyp