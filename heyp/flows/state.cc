#include "heyp/flows/state.h"

#include <algorithm>

#include "absl/strings/substitute.h"
#include "heyp/log/logging.h"

namespace heyp {

AggState::AggState(const proto::FlowMarker& flow, bool smooth_usage)
    : smooth_usage_(smooth_usage) {
  *cur_.mutable_flow() = flow;
}

const proto::FlowMarker& AggState::flow() const { return cur_.flow(); }
absl::Time AggState::updated_time() const { return updated_time_; }
const proto::FlowInfo& AggState::cur() const { return cur_; }

void AggState::UpdateUsage(const Update u, absl::Duration usage_history_window,
                           const DemandPredictor& demand_predictor) {
  const int64_t cum_usage_bytes = u.cum_hipri_usage_bytes + u.cum_lopri_usage_bytes;
  const bool is_lopri = (u.cum_hipri_usage_bytes == cur_.cum_hipri_usage_bytes()) &&
                        (u.cum_lopri_usage_bytes > cur_.cum_lopri_usage_bytes());

  if (u.time < updated_time_) {
    LOG(WARNING) << absl::Substitute(
        "got update ($0, $1) older than last update ($2, $3)",
        absl::FormatTime(u.time, absl::UTCTimeZone()), cum_usage_bytes,
        absl::FormatTime(updated_time_, absl::UTCTimeZone()), cur_.cum_usage_bytes());
    return;
  }

  CHECK_GE(u.cum_hipri_usage_bytes, cur_.cum_hipri_usage_bytes());
  CHECK_GE(u.cum_lopri_usage_bytes, cur_.cum_lopri_usage_bytes());

  double measured_usage_bps = u.sum_child_usage_bps;

  if (was_updated_) {
    const int64_t usage_bits = 8 * (cum_usage_bytes - cur_.cum_usage_bytes());
    const absl::Duration dur = u.time - updated_time_;
    if (dur > absl::ZeroDuration()) {
      const double measured_mean_usage_bps = usage_bits / absl::ToDoubleSeconds(dur);
      measured_usage_bps = std::max<double>(measured_mean_usage_bps, measured_usage_bps);
    }
  } else {
    was_updated_ = true;
    updated_time_ = u.time;
    cur_.set_currently_lopri(is_lopri);
    cur_.set_cum_usage_bytes(cum_usage_bytes);
    cur_.set_cum_hipri_usage_bytes(u.cum_hipri_usage_bytes);
    cur_.set_cum_lopri_usage_bytes(u.cum_lopri_usage_bytes);

    if (measured_usage_bps == 0 /* == instantaneous_usage_bps */) {
      return;  // likely no usage data => wait to estimate usage
    }
  }

  if (!have_bps_ || !smooth_usage_) {
    cur_.set_ewma_usage_bps(measured_usage_bps);
    have_bps_ = true;
  } else if (smooth_usage_) {
    constexpr double alpha = 0.3;
    cur_.set_ewma_usage_bps(alpha * (measured_usage_bps) +
                            (1 - alpha) * cur_.ewma_usage_bps());
  }

  updated_time_ = u.time;
  cur_.set_currently_lopri(is_lopri);
  cur_.set_cum_usage_bytes(cum_usage_bytes);
  cur_.set_cum_hipri_usage_bytes(u.cum_hipri_usage_bytes);
  cur_.set_cum_lopri_usage_bytes(u.cum_lopri_usage_bytes);

  usage_history_.push_back({u.time, cur_.ewma_usage_bps()});

  // Garbage collect old entries, but allow some delay.
  if (u.time - usage_history_.front().time > 2 * usage_history_window) {
    absl::Time min_time = u.time - usage_history_window;
    size_t keep_from = usage_history_.size();
    for (size_t i = 0; i < usage_history_.size(); ++i) {
      if (usage_history_[i].time >= min_time) {
        keep_from = i;
        break;
      }
    }
    ssize_t num_keep = usage_history_.size() - keep_from;
    for (ssize_t i = 0; i < num_keep; ++i) {
      usage_history_[i] = usage_history_[keep_from + i];
    }
    usage_history_.resize(num_keep);
  }

  cur_.set_predicted_demand_bps(demand_predictor.FromUsage(u.time, usage_history_));
}

LeafState::LeafState(const proto::FlowMarker& flow) : impl_(flow, true) {}

const proto::FlowMarker& LeafState::flow() const { return impl_.flow(); }
absl::Time LeafState::updated_time() const { return impl_.updated_time(); }
const proto::FlowInfo& LeafState::cur() const { return impl_.cur(); }

void LeafState::UpdateUsage(const Update u, absl::Duration usage_history_window,
                            const DemandPredictor& demand_predictor) {
  const proto::FlowInfo& c = impl_.cur();
  int64_t bps_diff = u.cum_usage_bytes - c.cum_usage_bytes();
  int64_t cum_hipri_usage_bytes = c.cum_hipri_usage_bytes();
  int64_t cum_lopri_usage_bytes = c.cum_lopri_usage_bytes();
  if (u.is_lopri) {
    cum_lopri_usage_bytes += bps_diff;
  } else {
    cum_hipri_usage_bytes += bps_diff;
  }

  impl_.UpdateUsage(
      {
          .time = u.time,
          .sum_child_usage_bps = u.instantaneous_usage_bps,
          .cum_hipri_usage_bytes = cum_hipri_usage_bytes,
          .cum_lopri_usage_bytes = cum_lopri_usage_bytes,
      },
      usage_history_window, demand_predictor);
}

}  // namespace heyp