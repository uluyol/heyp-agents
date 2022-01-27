#include "heyp/alg/downgrade/impl-knapsack-solver.h"

#include <algorithm>

#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/downgrade/formatters.h"
#include "ortools/algorithms/knapsack_solver.h"

namespace heyp {

std::vector<bool> KnapsackSolverDowngradeSelector::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger) {
  const bool should_debug = DebugQosAndRateLimitSelection();
  const auto& agg_children = agg_info.children();
  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "parent: {}", agg_info.parent().DebugString());
    SPDLOG_LOGGER_INFO(logger, "children: {}",
                       absl::StrJoin(agg_children, "\n", absl::StreamFormatter()));
  }

  operations_research::KnapsackSolver solver("pick-lopri");
  if (time_limit_sec_ > 0) {
    solver.set_time_limit(time_limit_sec_);
  }

  int64_t total_demand = 0;
  std::vector<int64_t> demands(agg_children.size(), 0);
  for (size_t i = 0; i < agg_children.size(); ++i) {
    const auto& c = agg_children[i];
    total_demand += c.volume_bps;
    demands[i] = c.volume_bps;
  }

  int64_t want_demand = want_frac_lopri * total_demand;

  solver.Init(demands, {demands}, {want_demand});
  int64_t got_total_demand = solver.Solve();

  int64_t double_check_total_demand = 0;
  std::vector<bool> lopri_children(agg_children.size(), false);
  for (size_t i = 0; i < agg_children.size(); ++i) {
    if (solver.BestSolutionContains(i)) {
      lopri_children[i] = true;
      double_check_total_demand += demands[i];
    }
  }

  if (!solver.IsSolutionOptimal()) {
    SPDLOG_LOGGER_INFO(logger, "did not find an optimal solution");
  }

  H_SPDLOG_CHECK_LE(logger, got_total_demand, want_demand);
  H_SPDLOG_CHECK_EQ(logger, double_check_total_demand, got_total_demand);

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "picked LOPRI assignment: {}",
                       absl::StrJoin(lopri_children, "", BitmapFormatter()));
  }

  return lopri_children;
}

}  // namespace heyp