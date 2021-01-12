#ifndef HEYP_FLOWS_STATE_H_
#define HEYP_FLOWS_STATE_H_

#include <cstdint>
#include <vector>

#include "absl/time/time.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

class FlowState {
 public:
  explicit FlowState(const proto::FlowMarker& flow);

  const proto::FlowMarker& flow() const;

  int64_t predicted_demand_bps() const;
  int64_t ewma_usage_bps() const;

  absl::Time updated_time() const;
  int64_t cum_usage_bytes() const;

  void UpdateUsage(absl::Time timestamp, int64_t instantaneous_usage_bps,
                   int64_t cum_usage_bytes, absl::Duration usage_history_window,
                   DemandPredictor* demand_predictor);

 private:
  proto::FlowMarker flow_;

  int64_t predicted_demand_bps_ = 0;
  int64_t ewma_usage_bps_ = 0;
  std::vector<UsageHistoryEntry> usage_history_;

  absl::Time updated_time_ = absl::InfinitePast();
  int64_t cum_usage_bytes_ = 0;

  bool was_updated_ = false;
  bool have_bps_ = false;
};

}  // namespace heyp

#endif  // HEYP_FLOWS_STATE_H_