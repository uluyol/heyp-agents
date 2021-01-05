#include "heyp/flows/state.h"

#include <algorithm>

#include "absl/strings/substitute.h"
#include "glog/logging.h"

namespace heyp {

FlowState::FlowState(const proto::FlowMarker& flow) : flow_(flow) {}

const proto::FlowMarker& FlowState::flow() const { return flow_; }

int64_t FlowState::predicted_demand_bps() const {
  return predicted_demand_bps_;
}

int64_t FlowState::ewma_usage_bps() const { return ewma_usage_bps_; }

absl::Time FlowState::updated_time() const { return updated_time_; }

int64_t FlowState::cum_usage_bytes() const { return cum_usage_bytes_; }

void FlowState::UpdateUsage(absl::Time timestamp, int64_t cum_usage_bytes,
                            absl::Duration usage_history_window,
                            DemandPredictor* demand_predictor) {
  if (was_updated_) {
    if (timestamp < updated_time_) {
      LOG(WARNING) << absl::Substitute(
          "got update ($0, $1) older than last update ($2, $3)",
          absl::FormatTime(timestamp, absl::UTCTimeZone()), cum_usage_bytes,
          absl::FormatTime(updated_time_, absl::UTCTimeZone()),
          cum_usage_bytes_);
      return;
    }
  } else {
    was_updated_ = true;
    updated_time_ = timestamp;
    cum_usage_bytes_ = cum_usage_bytes;
    return;  // need two bytes measurements to compute bps
  }

  const int64_t usage_bits = 8 * (cum_usage_bytes - cum_usage_bytes_);
  const absl::Duration dur = timestamp - updated_time_;
  const double measured_usage_bps = usage_bits / absl::ToDoubleSeconds(dur);

  if (!have_bps_) {
    ewma_usage_bps_ = measured_usage_bps;
    have_bps_ = true;
  } else {
    constexpr double alpha = 0.3;
    ewma_usage_bps_ =
        alpha * (measured_usage_bps) + (1 - alpha) * ewma_usage_bps_;
  }

  updated_time_ = timestamp;
  cum_usage_bytes_ = cum_usage_bytes;
  usage_history_.push_back({timestamp, ewma_usage_bps_});

  // Garbage collect old entries, but allow some delay.
  if (timestamp - usage_history_.front().time > 2 * usage_history_window) {
    absl::Time min_time = timestamp - usage_history_window;
    size_t keep_from = usage_history_.size();
    for (size_t i = 0; i < usage_history_.size(); i++) {
      if (usage_history_[i].time >= min_time) {
        keep_from = i;
        break;
      }
    }
    ssize_t num_keep = usage_history_.size() - keep_from;
    for (ssize_t i = 0; i < num_keep; i++) {
      usage_history_[i] = usage_history_[keep_from + i];
    }
    usage_history_.resize(num_keep);
  }

  if (demand_predictor == nullptr) {
    predicted_demand_bps_ = 0;
  } else {
    predicted_demand_bps_ =
        demand_predictor->FromUsage(timestamp, usage_history_);
  }
}

}  // namespace heyp