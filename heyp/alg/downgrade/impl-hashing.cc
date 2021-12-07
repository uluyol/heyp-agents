#include "heyp/alg/downgrade/impl-hashing.h"

#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/downgrade/formatters.h"

namespace heyp {

std::vector<bool> HashingDowngradeSelector::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "parent: {}", agg_info.parent().DebugString());
    SPDLOG_LOGGER_INFO(logger, "children: {}",
                       absl::StrJoin(agg_info.children(), "\n", absl::StreamFormatter()));
    SPDLOG_LOGGER_INFO(logger, "initial lopri ring: {}", lopri_.ToString());
  }

  lopri_.UpdateFrac(want_frac_lopri);
  RingRanges lopri_ranges = lopri_.MatchingRanges();

  const auto& agg_children = agg_info.children();
  std::vector<bool> lopri_children(agg_children.size(), false);
  for (size_t i = 0; i < agg_children.size(); ++i) {
    lopri_children[i] = lopri_ranges.Contains(agg_children[i].child_id);
  }

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "revised lopri ring: {}", lopri_.ToString());
    SPDLOG_LOGGER_INFO(logger, "picked LOPRI assignment: {}",
                       absl::StrJoin(lopri_children, "", BitmapFormatter()));
  }

  return lopri_children;
}

}  // namespace heyp
