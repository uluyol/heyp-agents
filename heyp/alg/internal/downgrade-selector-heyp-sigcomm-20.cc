#include "heyp/alg/internal/downgrade-selector-heyp-sigcomm-20.h"

#include <algorithm>

#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/internal/formatters.h"
#include "heyp/alg/internal/greedy-assign.h"

namespace heyp {
namespace internal {

template <FVSource vol_source>
std::vector<bool> HeypSigcomm20DowngradeSelector<vol_source>::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger) {
  const bool should_debug = DebugQosAndRateLimitSelection();
  const auto& agg_children = agg_info.children();

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "parent: {}", agg_info.parent().DebugString());
    SPDLOG_LOGGER_INFO(logger, "children: {}",
                       absl::StrJoin(agg_children, "\n", FlowInfoFormatter()));
  }

  std::vector<bool> lopri_children(agg_children.size(), false);
  int64_t total_demand = 0;
  int64_t lopri_demand = 0;
  std::vector<size_t> children_sorted_by_dec_demand(agg_children.size(), 0);
  for (size_t i = 0; i < agg_children.size(); ++i) {
    children_sorted_by_dec_demand[i] = i;
    const auto& c = agg_children[i];
    total_demand += GetFlowVolume(c, vol_source);
    if (c.currently_lopri()) {
      lopri_children[i] = true;
      lopri_demand += GetFlowVolume(c, vol_source);
    }
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
              int64_t lhs_demand = GetFlowVolume(agg_children[lhs], vol_source);
              int64_t rhs_demand = GetFlowVolume(agg_children[rhs], vol_source);
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
    GreedyAssignToMinimizeGap<false, vol_source>(
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
    GreedyAssignToMinimizeGap<true, vol_source>(
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

template std::vector<bool>
HeypSigcomm20DowngradeSelector<FVSource::kPredictedDemand>::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger);

template std::vector<bool>
HeypSigcomm20DowngradeSelector<FVSource::kUsage>::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger);

}  // namespace internal
}  // namespace heyp