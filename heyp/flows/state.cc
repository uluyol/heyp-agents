#include "heyp/flows/state.h"

#include <algorithm>

#include "absl/strings/substitute.h"
#include "glog/logging.h"

namespace heyp {

FlowState::FlowState(const proto::FlowMarker& flow) : cur_({.flow = flow}) {}

const proto::FlowMarker& FlowState::flow() const { return cur_.flow; }
const FlowStateSnapshot& FlowState::cur() const { return cur_; }

void FlowState::UpdateUsage(const Update u, absl::Duration usage_history_window,
                            const DemandPredictor& demand_predictor) {
  double measured_usage_bps = u.instantaneous_usage_bps;
  if (was_updated_) {
    if (u.time < cur_.updated_time) {
      LOG(WARNING) << absl::Substitute(
          "got update ($0, $1) older than last update ($2, $3)",
          absl::FormatTime(u.time, absl::UTCTimeZone()), u.cum_usage_bytes,
          absl::FormatTime(cur_.updated_time, absl::UTCTimeZone()),
          cur_.cum_usage_bytes);
      return;
    }
    const int64_t usage_bits = 8 * (u.cum_usage_bytes - cur_.cum_usage_bytes);
    const absl::Duration dur = u.time - cur_.updated_time;
    const double measured_mean_usage_bps =
        usage_bits / absl::ToDoubleSeconds(dur);
    measured_usage_bps =
        std::max<double>(measured_mean_usage_bps, measured_usage_bps);
  } else {
    was_updated_ = true;
    cur_.updated_time = u.time;
    if (u.is_lopri) {
      cur_.cum_lopri_usage_bytes += u.cum_usage_bytes - cur_.cum_usage_bytes;
      cur_.currently_lopri = true;
    } else {
      cur_.cum_hipri_usage_bytes += u.cum_usage_bytes - cur_.cum_usage_bytes;
      cur_.currently_lopri = false;
    }
    cur_.cum_usage_bytes = u.cum_usage_bytes;
    if (measured_usage_bps == 0 /* == instantaneous_usage_bps */) {
      return;  // likely no usage data => wait to estimate usage
    }
  }

  if (!have_bps_) {
    cur_.ewma_usage_bps = measured_usage_bps;
    have_bps_ = true;
  } else {
    constexpr double alpha = 0.3;
    cur_.ewma_usage_bps =
        alpha * (measured_usage_bps) + (1 - alpha) * cur_.ewma_usage_bps;
  }

  cur_.updated_time = u.time;
  if (u.is_lopri) {
    cur_.cum_lopri_usage_bytes += u.cum_usage_bytes - cur_.cum_usage_bytes;
    cur_.currently_lopri = true;
  } else {
    cur_.cum_hipri_usage_bytes += u.cum_usage_bytes - cur_.cum_usage_bytes;
    cur_.currently_lopri = false;
  }
  cur_.cum_usage_bytes = u.cum_usage_bytes;
  usage_history_.push_back({u.time, cur_.ewma_usage_bps});

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

  cur_.predicted_demand_bps =
      demand_predictor.FromUsage(u.time, usage_history_);
}

}  // namespace heyp