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

int64_t FlowState::cum_hipri_usage_bytes() const {
  return cum_hipri_usage_bytes_;
}

int64_t FlowState::cum_lopri_usage_bytes() const {
  return cum_lopri_usage_bytes_;
}

bool FlowState::currently_lopri() const { return currently_lopri_; }

void FlowState::UpdateUsage(const Update u, absl::Duration usage_history_window,
                            const DemandPredictor& demand_predictor) {
  double measured_usage_bps = u.instantaneous_usage_bps;
  if (was_updated_) {
    if (u.time < updated_time_) {
      LOG(WARNING) << absl::Substitute(
          "got update ($0, $1) older than last update ($2, $3)",
          absl::FormatTime(u.time, absl::UTCTimeZone()), u.cum_usage_bytes,
          absl::FormatTime(updated_time_, absl::UTCTimeZone()),
          cum_usage_bytes_);
      return;
    }
    const int64_t usage_bits = 8 * (u.cum_usage_bytes - cum_usage_bytes_);
    const absl::Duration dur = u.time - updated_time_;
    const double measured_mean_usage_bps =
        usage_bits / absl::ToDoubleSeconds(dur);
    measured_usage_bps =
        std::max<double>(measured_mean_usage_bps, measured_usage_bps);
  } else {
    was_updated_ = true;
    updated_time_ = u.time;
    if (u.is_lopri) {
      cum_lopri_usage_bytes_ += u.cum_usage_bytes - cum_usage_bytes_;
      currently_lopri_ = true;
    } else {
      cum_hipri_usage_bytes_ += u.cum_usage_bytes - cum_usage_bytes_;
      currently_lopri_ = false;
    }
    cum_usage_bytes_ = u.cum_usage_bytes;
    if (measured_usage_bps == 0 /* == instantaneous_usage_bps */) {
      return;  // likely no usage data => wait to estimate usage
    }
  }

  if (!have_bps_) {
    ewma_usage_bps_ = measured_usage_bps;
    have_bps_ = true;
  } else {
    constexpr double alpha = 0.3;
    ewma_usage_bps_ =
        alpha * (measured_usage_bps) + (1 - alpha) * ewma_usage_bps_;
  }

  updated_time_ = u.time;
  if (u.is_lopri) {
    cum_lopri_usage_bytes_ += u.cum_usage_bytes - cum_usage_bytes_;
    currently_lopri_ = true;
  } else {
    cum_hipri_usage_bytes_ += u.cum_usage_bytes - cum_usage_bytes_;
    currently_lopri_ = false;
  }
  cum_usage_bytes_ = u.cum_usage_bytes;
  usage_history_.push_back({u.time, ewma_usage_bps_});

  // Garbage collect old entries, but allow some delay.
  if (u.time - usage_history_.front().time > 2 * usage_history_window) {
    absl::Time min_time = u.time - usage_history_window;
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

  predicted_demand_bps_ = demand_predictor.FromUsage(u.time, usage_history_);
}

}  // namespace heyp