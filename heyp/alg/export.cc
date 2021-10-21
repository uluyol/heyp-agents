// This file contains definitions that are exported for use in Go.

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <vector>

#include "ortools/algorithms/knapsack_solver.h"
#include "routing-algos/alg/max-min-fairness.h"

extern "C" {
int64_t HeypKnapsackUsageLOPRI(int64_t* demands, size_t num_demands,
                               double want_frac_lopri, uint8_t* out_is_lopri) {
  std::optional<operations_research::KnapsackSolver> solver;

  if (num_demands <= 64) {
    solver.emplace(operations_research::KnapsackSolver::KNAPSACK_64ITEMS_SOLVER,
                   "pick-lopri");
  } else {
    solver.emplace("pick-lopri");
  }

  std::vector<int64_t> demands_vec(demands, demands + num_demands);
  int64_t total_demand = 0;
  for (int64_t demand : demands_vec) {
    total_demand += demand;
  }
  int64_t want_demand = want_frac_lopri * total_demand;
  solver->Init(demands_vec, {demands_vec}, {want_demand});
  int64_t ret = solver->Solve();
  for (size_t i = 0; i < demands_vec.size(); ++i) {
    uint8_t picked = 0;
    if (solver->BestSolutionContains(i)) {
      picked = 1;
    }
    out_is_lopri[i] = picked;
  }
  return ret;
}

int64_t HeypMaxMinFairWaterlevel(int64_t admission, int64_t* demands,
                                 size_t num_demands) {
  std::vector<int64_t> demands_vec(demands, demands + num_demands);
  routing_algos::SingleLinkMaxMinFairnessProblem problem;
  return problem.ComputeWaterlevel(admission, {demands_vec});
}
}