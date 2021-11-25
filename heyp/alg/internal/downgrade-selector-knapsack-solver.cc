#include "heyp/alg/internal/downgrade-selector-knapsack-solver.h"

#include <algorithm>

#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/internal/formatters.h"
#include "ortools/algorithms/knapsack_solver.h"

namespace heyp {
namespace internal {

std::vector<bool> KnapsackSolverDowngradeSelector::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger) {
  const bool should_debug = DebugQosAndRateLimitSelection();
  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "parent: {}", agg_info.parent().DebugString());
    SPDLOG_LOGGER_INFO(logger, "children: {}",
                       absl::StrJoin(agg_info.children(), "\n", FlowInfoFormatter()));
  }

  absl::optional<operations_research::KnapsackSolver> solver;

  if (agg_info.children_size() <= 64) {
    solver.emplace(operations_research::KnapsackSolver::KNAPSACK_64ITEMS_SOLVER,
                   "pick-lopri");
  } else {
    solver.emplace("pick-lopri");
  }

  int64_t total_demand = 0;
  std::vector<int64_t> demands(agg_info.children_size(), 0);
  for (size_t i = 0; i < agg_info.children_size(); ++i) {
    const auto& c = agg_info.children(i);
    total_demand += c.predicted_demand_bps();
    demands[i] = c.predicted_demand_bps();
  }

  int64_t want_demand = want_frac_lopri * total_demand;

  solver->Init(demands, {demands}, {want_demand});
  int64_t got_total_demand = solver->Solve();

  int64_t double_check_total_demand = 0;
  std::vector<bool> lopri_children(agg_info.children_size(), false);
  for (size_t i = 0; i < agg_info.children_size(); ++i) {
    if (solver->BestSolutionContains(i)) {
      lopri_children[i] = true;
      double_check_total_demand += demands[i];
    }
  }

  H_SPDLOG_CHECK_LE(logger, got_total_demand, want_demand);
  H_SPDLOG_CHECK_EQ(logger, double_check_total_demand, got_total_demand);

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "picked LOPRI assignment: {}",
                       absl::StrJoin(lopri_children, "", BitmapFormatter()));
  }

  return lopri_children;
}

}  // namespace internal
}  // namespace heyp