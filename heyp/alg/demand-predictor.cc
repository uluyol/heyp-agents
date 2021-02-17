#include "heyp/alg/demand-predictor.h"

#include "absl/base/macros.h"

namespace heyp {

bool operator==(const UsageHistoryEntry& lhs, const UsageHistoryEntry& rhs) {
  return (lhs.time == rhs.time) && (lhs.bps == rhs.bps);
}

std::ostream& operator<<(std::ostream& os, const UsageHistoryEntry& e) {
  return os << "(" << e.time << ", " << e.bps << ")";
}

BweDemandPredictor::BweDemandPredictor(absl::Duration time_window,
                                       double usage_multiplier, int64_t min_demand_bps)
    : time_window_(time_window),
      usage_multiplier_(usage_multiplier),
      min_demand_bps_(min_demand_bps) {
  ABSL_ASSERT(usage_multiplier_ > 0);
  ABSL_ASSERT(min_demand_bps_ >= 0);
}

int64_t BweDemandPredictor::FromUsage(
    absl::Time now, absl::Span<const UsageHistoryEntry> usage_history) const {
  double max_usage_bps = 0;
  for (ssize_t i = usage_history.size() - 1; i >= 0; i--) {
    if (usage_history[i].time >= now - time_window_) {
      max_usage_bps = std::max<double>(max_usage_bps, usage_history[i].bps);
    }
  }

  double est = max_usage_bps * usage_multiplier_;
  if (est > min_demand_bps_) {
    return est;
  }
  return min_demand_bps_;
}

}  // namespace heyp