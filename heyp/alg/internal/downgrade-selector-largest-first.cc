#include "heyp/alg/internal/downgrade-selector-largest-first.h"

#include <algorithm>

#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/internal/formatters.h"
#include "heyp/alg/internal/greedy-assign.h"

namespace heyp {
namespace internal {

std::vector<bool> LargestFirstDowngradeSelector::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger) {
  const bool should_debug = DebugQosAndRateLimitSelection();
  const auto& agg_children = agg_info.children();

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "parent: {}", agg_info.parent().DebugString());
    SPDLOG_LOGGER_INFO(logger, "children: {}",
                       absl::StrJoin(agg_children, "\n", absl::StreamFormatter()));
  }

  int64_t total_demand = 0;
  std::vector<size_t> children_sorted_by_dec_demand(agg_children.size(), 0);
  for (size_t i = 0; i < agg_children.size(); ++i) {
    children_sorted_by_dec_demand[i] = i;
    const auto& c = agg_children[i];
    total_demand += c.volume_bps;
  }

  if (total_demand == 0) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "no demand");
    }
    // Don't use LOPRI if all demand is zero.
    return std::vector<bool>(agg_children.size(), false);
  }

  std::sort(children_sorted_by_dec_demand.begin(), children_sorted_by_dec_demand.end(),
            [&agg_children](size_t lhs, size_t rhs) -> bool {
              int64_t lhs_demand = agg_children[lhs].volume_bps;
              int64_t rhs_demand = agg_children[rhs].volume_bps;
              if (lhs_demand == rhs_demand) {
                return lhs > rhs;
              }
              return lhs_demand > rhs_demand;
            });

  std::vector<bool> lopri_children(agg_children.size(), false);
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

}  // namespace internal
}  // namespace heyp