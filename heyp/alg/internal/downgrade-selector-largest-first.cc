#include "heyp/alg/internal/downgrade-selector-largest-first.h"

#include <algorithm>

#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/internal/formatters.h"
#include "heyp/alg/internal/greedy-assign.h"

namespace heyp {
namespace internal {

template <FVSource vol_source>
std::vector<bool> LargestFirstDowngradeSelector<vol_source>::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "parent: {}", agg_info.parent().DebugString());
    SPDLOG_LOGGER_INFO(logger, "children: {}",
                       absl::StrJoin(agg_info.children(), "\n", FlowInfoFormatter()));
  }

  int64_t total_demand = 0;
  std::vector<size_t> children_sorted_by_dec_demand(agg_info.children_size(), 0);
  for (size_t i = 0; i < agg_info.children_size(); ++i) {
    children_sorted_by_dec_demand[i] = i;
    const auto& c = agg_info.children(i);
    total_demand += GetFlowVolume(c, vol_source);
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
              int64_t lhs_demand = GetFlowVolume(agg_info.children(lhs), vol_source);
              int64_t rhs_demand = GetFlowVolume(agg_info.children(rhs), vol_source);
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
  GreedyAssignToMinimizeGap<true, vol_source>(
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

template std::vector<bool>
LargestFirstDowngradeSelector<FVSource::kPredictedDemand>::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger);

template std::vector<bool>
LargestFirstDowngradeSelector<FVSource::kUsage>::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger);

}  // namespace internal
}  // namespace heyp