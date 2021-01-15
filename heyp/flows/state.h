#ifndef HEYP_FLOWS_STATE_H_
#define HEYP_FLOWS_STATE_H_

#include <cstdint>
#include <vector>

#include "absl/time/time.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

struct FlowStateSnapshot {
  proto::FlowMarker flow;
  int64_t predicted_demand_bps = 0;
  int64_t ewma_usage_bps = 0;

  absl::Time updated_time = absl::InfinitePast();

  // cum_usage_bytes = cum_hipri_usage_bytes + cum_lopri_usage_bytes
  int64_t cum_usage_bytes = 0;
  int64_t cum_hipri_usage_bytes = 0;
  int64_t cum_lopri_usage_bytes = 0;

  bool currently_lopri = false;
};

class FlowState {
 public:
  explicit FlowState(const proto::FlowMarker& flow);

  const proto::FlowMarker& flow() const;
  const FlowStateSnapshot& cur() const;

  struct Update {
    absl::Time time;
    int64_t cum_usage_bytes = 0;
    int64_t instantaneous_usage_bps = 0;  // optional
    bool is_lopri = false;                // optional
  };

  void UpdateUsage(const Update u, absl::Duration usage_history_window,
                   const DemandPredictor& demand_predictor);

 private:
  std::vector<UsageHistoryEntry> usage_history_;
  FlowStateSnapshot cur_;

  bool was_updated_ = false;
  bool have_bps_ = false;
};

}  // namespace heyp

#endif  // HEYP_FLOWS_STATE_H_