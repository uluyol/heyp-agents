#include <stdint.h>

#include <random>

#include "absl/functional/function_ref.h"
#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "heyp/alg/fairness/max-min-fairness.h"

namespace heyp {
namespace {

static void BenchSingleLinkMaxMinFairnessProblem(
    benchmark::State& state, absl::FunctionRef<int64_t(int64_t)> total_to_capacity_fn,
    SingleLinkMaxMinFairnessProblem* problem) {
  std::vector<int64_t> demands(state.range(0), 0);
  std::mt19937_64 rng(0);
  int64_t total = 0;
  for (size_t i = 0; i < demands.size(); i++) {
    demands[i] = absl::Uniform<int64_t>(rng, 0, 1000);
    total += demands[i];
  }
  const int64_t capacity = total_to_capacity_fn(total);
  std::vector<int64_t> allocs;
  for (auto _ : state) {
    int64_t waterlevel = problem->ComputeWaterlevel(capacity, demands);
    problem->SetAllocations(waterlevel, demands, &allocs);
  }
  benchmark::DoNotOptimize(allocs);
}

// With Tiny Flow Optimization //

static void BM_SingleLinkMaxMinFairnessProblem_WithTinyOpt_Demand_Eq_DoubleLimit(
    benchmark::State& state) {
  SingleLinkMaxMinFairnessProblem problem({.enable_tiny_flow_opt = true});
  BenchSingleLinkMaxMinFairnessProblem(
      state, [](int64_t total) { return total / 2; }, &problem);
}

BENCHMARK(BM_SingleLinkMaxMinFairnessProblem_WithTinyOpt_Demand_Eq_DoubleLimit)
    ->RangeMultiplier(10)
    ->Range(10, 10000);

static void BM_SingleLinkMaxMinFairnessProblem_WithTinyOpt_Demand_Eq_Limit(
    benchmark::State& state) {
  SingleLinkMaxMinFairnessProblem problem({.enable_tiny_flow_opt = true});
  BenchSingleLinkMaxMinFairnessProblem(
      state, [](int64_t total) { return total; }, &problem);
}

BENCHMARK(BM_SingleLinkMaxMinFairnessProblem_WithTinyOpt_Demand_Eq_Limit)
    ->RangeMultiplier(10)
    ->Range(10, 10000);

static void BM_SingleLinkMaxMinFairnessProblem_WithTinyOpt_Demand_Eq_HalfLimit(
    benchmark::State& state) {
  SingleLinkMaxMinFairnessProblem problem({.enable_tiny_flow_opt = true});
  BenchSingleLinkMaxMinFairnessProblem(
      state, [](int64_t total) { return total * 2; }, &problem);
}

BENCHMARK(BM_SingleLinkMaxMinFairnessProblem_WithTinyOpt_Demand_Eq_HalfLimit)
    ->RangeMultiplier(10)
    ->Range(10, 10000);

// Without Tiny Flow Optimization //

static void BM_SingleLinkMaxMinFairnessProblem_NoTinyOpt_Demand_Eq_DoubleLimit(
    benchmark::State& state) {
  SingleLinkMaxMinFairnessProblem problem({.enable_tiny_flow_opt = false});
  BenchSingleLinkMaxMinFairnessProblem(
      state, [](int64_t total) { return total / 2; }, &problem);
}

BENCHMARK(BM_SingleLinkMaxMinFairnessProblem_NoTinyOpt_Demand_Eq_DoubleLimit)
    ->RangeMultiplier(10)
    ->Range(10, 10000);

static void BM_SingleLinkMaxMinFairnessProblem_NoTinyOpt_Demand_Eq_Limit(
    benchmark::State& state) {
  SingleLinkMaxMinFairnessProblem problem({.enable_tiny_flow_opt = false});
  BenchSingleLinkMaxMinFairnessProblem(
      state, [](int64_t total) { return total; }, &problem);
}

BENCHMARK(BM_SingleLinkMaxMinFairnessProblem_NoTinyOpt_Demand_Eq_Limit)
    ->RangeMultiplier(10)
    ->Range(10, 10000);

static void BM_SingleLinkMaxMinFairnessProblem_NoTinyOpt_Demand_Eq_HalfLimit(
    benchmark::State& state) {
  SingleLinkMaxMinFairnessProblem problem({.enable_tiny_flow_opt = false});
  BenchSingleLinkMaxMinFairnessProblem(
      state, [](int64_t total) { return total * 2; }, &problem);
}

BENCHMARK(BM_SingleLinkMaxMinFairnessProblem_NoTinyOpt_Demand_Eq_HalfLimit)
    ->RangeMultiplier(10)
    ->Range(10, 10000);

}  // namespace
}  // namespace heyp