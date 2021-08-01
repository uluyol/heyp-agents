#include "heyp/alg/qos-downgrade.h"

#include <algorithm>

#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/heyp.pb.h"
#include "ortools/algorithms/knapsack_solver.h"

namespace heyp {

template <bool StateToIncrease>
void GreedyAssignToMinimizeGap(GreedyAssignToMinimizeGapArgs args,
                               std::vector<bool>& lopri_children,
                               bool punish_only_largest) {
  for (size_t i = 0; i < args.children_sorted_by_dec_demand.size(); ++i) {
    const size_t child_i = args.children_sorted_by_dec_demand[i];
    if (lopri_children[child_i] == StateToIncrease) {
      continue;  // child already belongs to our bin, don't flip
    }
    // Try to flip child_i to our bin.
    int64_t next_demand =
        args.cur_demand + args.agg_info.children(child_i).predicted_demand_bps();

    if (next_demand > args.want_demand) {
      bool exceeds_twice_gap = next_demand > 2 * args.want_demand - args.cur_demand;

      if (punish_only_largest) {
        if (!exceeds_twice_gap) {
          // safe to flip
          lopri_children[child_i] = StateToIncrease;
          args.cur_demand = next_demand;
        }
        return;
      }

      // Don't flip child_i if there are more children with smaller demands to flip.
      bool have_children_with_less_demand =
          i < args.children_sorted_by_dec_demand.size() - 1;

      if (have_children_with_less_demand || exceeds_twice_gap) {
        continue;  // flipping child_i overshoots the goal
      }
    }
    // Safe to flip child_i;
    lopri_children[child_i] = StateToIncrease;
    args.cur_demand = next_demand;
  }
}

// Instantiate both (true/false) variants
template void GreedyAssignToMinimizeGap<false>(GreedyAssignToMinimizeGapArgs args,
                                               std::vector<bool>& lopri_children,
                                               bool punish_only_largest);
template void GreedyAssignToMinimizeGap<true>(GreedyAssignToMinimizeGapArgs args,
                                              std::vector<bool>& lopri_children,
                                              bool punish_only_largest);

struct BitmapFormatter {
  void operator()(std::string* out, bool b) {
    if (b) {
      out->push_back('1');
    } else {
      out->push_back('0');
    }
  }
};

static std::vector<bool> HeypSigcomm20PickLOPRIChildren(const proto::AggInfo& agg_info,
                                                        const double want_frac_lopri,
                                                        spdlog::logger* logger) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "agg_info: {}", agg_info.DebugString());
  }

  std::vector<bool> lopri_children(agg_info.children_size(), false);
  int64_t total_demand = 0;
  int64_t lopri_demand = 0;
  std::vector<size_t> children_sorted_by_dec_demand(agg_info.children_size(), 0);
  for (size_t i = 0; i < agg_info.children_size(); ++i) {
    children_sorted_by_dec_demand[i] = i;
    const auto& c = agg_info.children(i);
    total_demand += c.predicted_demand_bps();
    if (c.currently_lopri()) {
      lopri_children[i] = true;
      lopri_demand += c.predicted_demand_bps();
    }
  }

  if (total_demand == 0) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "no demand");
    }
    // Don't use LOPRI if all demand is zero.
    return std::vector<bool>(agg_info.children_size(), false);
  }

  std::sort(children_sorted_by_dec_demand.begin(), children_sorted_by_dec_demand.end(),
            [&agg_info](size_t lhs, size_t rhs) -> bool {
              int64_t lhs_demand = agg_info.children(lhs).predicted_demand_bps();
              int64_t rhs_demand = agg_info.children(rhs).predicted_demand_bps();
              if (lhs_demand == rhs_demand) {
                return lhs > rhs;
              }
              return lhs_demand > rhs_demand;
            });

  if (static_cast<double>(lopri_demand) / static_cast<double>(total_demand) >
      want_frac_lopri) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "move from LOPRI to HIPRI");
    }
    int64_t hipri_demand = total_demand - lopri_demand;
    int64_t want_demand = (1 - want_frac_lopri) * total_demand;
    GreedyAssignToMinimizeGap<false>(
        {
            .cur_demand = hipri_demand,
            .want_demand = want_demand,
            .children_sorted_by_dec_demand = children_sorted_by_dec_demand,
            .agg_info = agg_info,
        },
        lopri_children, false);
  } else {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "move from HIPRI to LOPRI");
    }
    int64_t want_demand = want_frac_lopri * total_demand;
    GreedyAssignToMinimizeGap<true>(
        {
            .cur_demand = lopri_demand,
            .want_demand = want_demand,
            .children_sorted_by_dec_demand = children_sorted_by_dec_demand,
            .agg_info = agg_info,
        },
        lopri_children, false);
  }

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "picked LOPRI assignment: {}",
                       absl::StrJoin(lopri_children, "", BitmapFormatter()));
  }

  return lopri_children;
}

static std::vector<bool> LargestFirstPickLOPRIChildren(const proto::AggInfo& agg_info,
                                                       const double want_frac_lopri,
                                                       spdlog::logger* logger) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "agg_info: {}", agg_info.DebugString());
  }

  int64_t total_demand = 0;
  std::vector<size_t> children_sorted_by_dec_demand(agg_info.children_size(), 0);
  for (size_t i = 0; i < agg_info.children_size(); ++i) {
    children_sorted_by_dec_demand[i] = i;
    const auto& c = agg_info.children(i);
    total_demand += c.predicted_demand_bps();
  }

  if (total_demand == 0) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "no demand");
    }
    // Don't use LOPRI if all demand is zero.
    return std::vector<bool>(agg_info.children_size(), false);
  }

  std::sort(children_sorted_by_dec_demand.begin(), children_sorted_by_dec_demand.end(),
            [&agg_info](size_t lhs, size_t rhs) -> bool {
              int64_t lhs_demand = agg_info.children(lhs).predicted_demand_bps();
              int64_t rhs_demand = agg_info.children(rhs).predicted_demand_bps();
              if (lhs_demand == rhs_demand) {
                return lhs > rhs;
              }
              return lhs_demand > rhs_demand;
            });

  std::vector<bool> lopri_children(agg_info.children_size(), false);
  int64_t lopri_demand = 0;
  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "move from HIPRI to LOPRI");
  }
  int64_t want_demand = want_frac_lopri * total_demand;
  GreedyAssignToMinimizeGap<true>(
      {
          .cur_demand = lopri_demand,
          .want_demand = want_demand,
          .children_sorted_by_dec_demand = children_sorted_by_dec_demand,
          .agg_info = agg_info,
      },
      lopri_children, true);

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "picked LOPRI assignment: {}",
                       absl::StrJoin(lopri_children, "", BitmapFormatter()));
  }

  return lopri_children;
}

std::vector<bool> KnapsackSolverPickLOPRIChildren(const proto::AggInfo& agg_info,
                                                  const double want_frac_lopri,
                                                  spdlog::logger* logger) {
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

  return lopri_children;
}

std::vector<bool> PickLOPRIChildren(const proto::AggInfo& agg_info,
                                    const double want_frac_lopri,
                                    const proto::DowngradeSelector& selector,
                                    spdlog::logger* logger) {
  switch (selector.type()) {
    case proto::DS_HEYP_SIGCOMM20:
      return HeypSigcomm20PickLOPRIChildren(agg_info, want_frac_lopri, logger);
    case proto::DS_KNAPSACK_SOLVER:
      return KnapsackSolverPickLOPRIChildren(agg_info, want_frac_lopri, logger);
    case proto::DS_LARGEST_FIRST:
      return LargestFirstPickLOPRIChildren(agg_info, want_frac_lopri, logger);
  }
  SPDLOG_LOGGER_CRITICAL(logger, "unsupported DowngradeSelectorType: {}",
                         selector.type());
  DumpStackTraceAndExit(5);
}

double FracAdmittedAtLOPRI(const proto::FlowInfo& parent,
                           const int64_t hipri_rate_limit_bps,
                           const int64_t lopri_rate_limit_bps) {
  bool maybe_admit = lopri_rate_limit_bps > 0;
  maybe_admit = maybe_admit && parent.predicted_demand_bps() > 0;
  maybe_admit = maybe_admit && parent.predicted_demand_bps() > hipri_rate_limit_bps;
  if (maybe_admit) {
    const double total_rate_limit_bps = hipri_rate_limit_bps + lopri_rate_limit_bps;
    const double total_admitted_demand_bps =
        std::min<double>(parent.predicted_demand_bps(), total_rate_limit_bps);
    return 1 - (hipri_rate_limit_bps / total_admitted_demand_bps);
  }
  return 0;
}

double FracAdmittedAtLOPRIToProbe(const proto::AggInfo& agg_info,
                                  const int64_t hipri_rate_limit_bps,
                                  const int64_t lopri_rate_limit_bps,
                                  const double demand_multiplier, const double lopri_frac,
                                  spdlog::logger* logger) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "agg_info: {}", agg_info.DebugString());
    SPDLOG_LOGGER_INFO(logger, "cur limits: ({}, {})", hipri_rate_limit_bps,
                       lopri_rate_limit_bps);
    SPDLOG_LOGGER_INFO(logger, "demand_multiplier: {}", demand_multiplier);
    SPDLOG_LOGGER_INFO(logger, "initial lopri_frac: {}", lopri_frac);
  }

  if (agg_info.parent().predicted_demand_bps() < hipri_rate_limit_bps) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "predicted demand < hipri rate limit ({} < {})",
                         agg_info.parent().predicted_demand_bps(), hipri_rate_limit_bps);
    }
    return lopri_frac;
  }
  if (agg_info.parent().predicted_demand_bps() >
      demand_multiplier * hipri_rate_limit_bps) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(
          logger, "predicted demand > demand multipler * hipri rate limit ({} > {})",
          agg_info.parent().predicted_demand_bps(),
          demand_multiplier * hipri_rate_limit_bps);
    }
    return lopri_frac;
  }
  if (agg_info.children_size() == 0) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "no children");
    }
    return lopri_frac;
  }
  int64_t smallest_child_demand_bps = agg_info.children(0).predicted_demand_bps();
  for (const proto::FlowInfo& child : agg_info.children()) {
    smallest_child_demand_bps =
        std::min(smallest_child_demand_bps, child.predicted_demand_bps());
  }

  if (smallest_child_demand_bps > lopri_rate_limit_bps) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "smallest child demand > lopri rate limit ({} > {})",
                         smallest_child_demand_bps, lopri_rate_limit_bps);
    }
    return lopri_frac;
  }

  double revised_frac = 1.00001 /* account for rounding error */ *
                        static_cast<double>(smallest_child_demand_bps) /
                        static_cast<double>(agg_info.parent().predicted_demand_bps());
  if (revised_frac > lopri_frac) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "revised lopri frac from {} to {}", lopri_frac,
                         revised_frac);
    }
    return revised_frac;
  } else if (should_debug) {
    SPDLOG_LOGGER_INFO(logger,
                       "existing lopri frac ({}) is larger than needed for probing ({})",
                       lopri_frac, revised_frac);
  }
  return lopri_frac;
}

}  // namespace heyp
