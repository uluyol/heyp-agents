#include "heyp/alg/internal/downgrade-selector-hashing.h"

#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/internal/formatters.h"

namespace heyp {
namespace internal {

template <FVSource vol_source>
std::vector<bool> HashingDowngradeSelector<vol_source>::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "parent: {}", agg_info.parent().DebugString());
    SPDLOG_LOGGER_INFO(logger, "children: {}",
                       absl::StrJoin(agg_info.children(), "\n", FlowInfoFormatter()));
    SPDLOG_LOGGER_INFO(logger, "initial lopri ring: {}", lopri_.ToString());
  }

  lopri_.UpdateFrac(want_frac_lopri);
  RingRanges lopri_ranges = lopri_.MatchingRanges();

  std::vector<bool> lopri_children(agg_info.children_size(), false);
  for (size_t i = 0; i < agg_info.children_size(); ++i) {
    lopri_children[i] = lopri_ranges.Contains(agg_info.children(i).flow().host_id());
  }

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "revised lopri ring: {}", lopri_.ToString());
    SPDLOG_LOGGER_INFO(logger, "picked LOPRI assignment: {}",
                       absl::StrJoin(lopri_children, "", BitmapFormatter()));
  }

  return lopri_children;
}

template std::vector<bool>
HashingDowngradeSelector<FVSource::kPredictedDemand>::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger);

template std::vector<bool> HashingDowngradeSelector<FVSource::kUsage>::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger);

}  // namespace internal
}  // namespace heyp
