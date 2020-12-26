#ifndef HEYP_ALG_DEMAND_PREDICTOR_H_
#define HEYP_ALG_DEMAND_PREDICTOR_H_

#include <cstdint>

#include "absl/time/time.h"
#include "absl/types/span.h"

namespace heyp {

struct UsageHistoryEntry {
  absl::Time time;
  int64_t bps = 0;
};

bool operator==(const UsageHistoryEntry& lhs, const UsageHistoryEntry& rhs);

class DemandPredictor {
 public:
  virtual ~DemandPredictor(){};

  virtual int64_t FromUsage(
      absl::Time now,
      absl::Span<const UsageHistoryEntry> usage_history) const = 0;
};

class BweDemandPredictor : public DemandPredictor {
 public:
  BweDemandPredictor(absl::Duration time_window, double usage_multiplier,
                     int64_t min_demand_bps);

  int64_t FromUsage(
      absl::Time now,
      absl::Span<const UsageHistoryEntry> usage_history) const override;

 private:
  const absl::Duration time_window_;
  const double usage_multiplier_;
  const int64_t min_demand_bps_;
};

}  // namespace heyp

#endif  // HEYP_ALG_DEMAND_PREDICTOR_H_