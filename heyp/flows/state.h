#ifndef HEYP_FLOWS_STATE_H_
#define HEYP_FLOWS_STATE_H_

#include <cstdint>
#include <vector>

#include "absl/time/time.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/proto/heyp.pb.h"
#include "spdlog/spdlog.h"

namespace heyp {

class AggState {
 public:
  explicit AggState(const proto::FlowMarker& flow, bool smooth_usage = false);

  const proto::FlowMarker& flow() const;
  absl::Time updated_time() const;
  const proto::FlowInfo& cur() const;

  struct Update {
    absl::Time time;
    int64_t sum_child_usage_bps = 0;
    int64_t cum_hipri_usage_bytes = 0;
    int64_t cum_lopri_usage_bytes = 0;
    const proto::FlowInfo::AuxInfo* aux = nullptr;  // optional
  };

  // UpdatesUpdate updates the demand and automatically sets currently_lopri
  // when there is an increase in LOPRI usage but no increase for HIPRI.
  void UpdateUsage(const Update u, absl::Duration usage_history_window,
                   const DemandPredictor& demand_predictor);

 protected:
  std::vector<UsageHistoryEntry> usage_history_;
  absl::Time updated_time_ = absl::InfinitePast();
  proto::FlowInfo cur_;
  const bool smooth_usage_ = false;
  spdlog::logger logger_;
  bool was_updated_ = false;
  bool have_bps_ = false;
};

class LeafState {
 public:
  explicit LeafState(const proto::FlowMarker& flow);

  const proto::FlowMarker& flow() const;
  absl::Time updated_time() const;
  const proto::FlowInfo& cur() const;

  struct Update {
    absl::Time time;
    int64_t cum_usage_bytes = 0;
    int64_t instantaneous_usage_bps = 0;            // optional
    bool is_lopri = false;                          // optional
    const proto::FlowInfo::AuxInfo* aux = nullptr;  // optional
  };

  void UpdateUsage(const Update u, absl::Duration usage_history_window,
                   const DemandPredictor& demand_predictor);

 private:
  AggState impl_;
  spdlog::logger logger_;
};

}  // namespace heyp

#endif  // HEYP_FLOWS_STATE_H_