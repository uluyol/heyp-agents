#ifndef HEYP_ALG_QOS_DOWNGRADE_H_
#define HEYP_ALG_QOS_DOWNGRADE_H_

#include <cstdint>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "heyp/alg/agg-info-views.h"
#include "heyp/alg/downgrade/iface.h"
#include "heyp/alg/flow-volume.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

class DowngradeSelector {
 public:
  explicit DowngradeSelector(const proto::DowngradeSelector& selector);

  std::vector<bool> PickLOPRIChildren(const proto::AggInfo& agg_info,
                                      const double want_frac_lopri);

 private:
  spdlog::logger logger_;
  std::unique_ptr<DowngradeSelectorImpl> impl_;
  const bool downgrade_jobs_;
  const bool downgrade_usage_;
};

class DowngradeFracController {
 public:
  explicit DowngradeFracController(const proto::DowngradeFracController& config);

  double TrafficFracToDowngradeRaw(double cur, double setpoint,
                                   double input2output_conversion,
                                   double max_task_usage_frac);

  double TrafficFracToDowngrade(double hipri_usage_bps, double lopri_usage_bps,
                                double hipri_rate_limit_bps, double max_task_usage_bps);

 private:
  proto::DowngradeFracController config_;
  spdlog::logger logger_;
};

// FracAdmittedAtLOPRI returns the fraction of traffic that should ideally be sent at
// LOPRI.
template <FVSource vol_source>
double FracAdmittedAtLOPRI(const proto::FlowInfo& parent,
                           const int64_t hipri_rate_limit_bps,
                           const int64_t lopri_rate_limit_bps);

// FracAdmittedAtLOPRIToProbe returns the LOPRI frac to use when the cluster controller
// should probe if there is additional demand by sending traffic as LOPRI.
//
// This condition triggers when hipri_rate_limit ≤ demand ≤ demand_multiplier *
// hipri_rate_limit.
//
// When triggered, the call will set lopri_frac to demand + the smallest child demand
// (if larger than the current value of lopri_frac and fits within lopri_rate_limit_bps).
//
// Else the return value will equal lopri_frac.
template <FVSource vol_source>
double FracAdmittedAtLOPRIToProbe(const proto::AggInfo& agg_info,
                                  const int64_t hipri_rate_limit_bps,
                                  const int64_t lopri_rate_limit_bps,
                                  const double demand_multiplier, const double lopri_frac,
                                  spdlog::logger* logger);

// Expect the following fields for SingleAggState:
//
//     struct SingleAggState {
//       proto::FlowAlloc alloc;
//       double frac_lopri = 0;
//       absl::Time last_time = absl::UnixEpoch();
//       int64_t last_cum_hipri_usage_bytes = 0;
//       int64_t last_cum_lopri_usage_bytes = 0;
//
//       // others are unused
//     };
template <typename SingleAggState>
int64_t HeypSigcomm20MaybeReviseLOPRIAdmission(
    double acceptable_measured_ratio_over_intended_ratio, absl::Time time,
    const proto::FlowInfo& parent, const SingleAggState& cur_state,
    spdlog::logger* logger) {
  if (time <= cur_state.last_time) {
    SPDLOG_LOGGER_WARN(logger, "cur time ({}) needs to be after last time ({})", time,
                       cur_state.last_time);
  } else if (cur_state.alloc.hipri_rate_limit_bps() > 0 && cur_state.frac_lopri > 0) {
    const double hipri_usage_bytes =
        parent.cum_hipri_usage_bytes() - cur_state.last_cum_hipri_usage_bytes;
    const double lopri_usage_bytes =
        parent.cum_lopri_usage_bytes() - cur_state.last_cum_lopri_usage_bytes;

    if (hipri_usage_bytes == 0) {
      SPDLOG_LOGGER_INFO(logger, "flow: {}: no HIPRI usage",
                         parent.flow().ShortDebugString());
    } else {
      // Now, if we try to send X Gbps as LOPRI, but only succeed at sending
      // 0.8 * X Gbps as LOPRI, this indicates that we have some congestion on
      // LOPRI. Therefore, we should lower the LOPRI rate limit to mitigate
      // the congestion.
      //
      // On the other hand, if we try to send X Gbps as LOPRI but end up
      // sending more, this indicates that we have underestimated the demand
      // and marked a smaller portion of traffic with LOPRI than we should
      // have. It says nothing about LOPRI or HIPRI being congested, so leave
      // the rate limits alone.
      ABSL_ASSERT(hipri_usage_bytes > 0);
      ABSL_ASSERT(cur_state.frac_lopri > 0);
      const double measured_ratio_over_intended_ratio =
          lopri_usage_bytes * (1 - cur_state.frac_lopri) /
          (hipri_usage_bytes * cur_state.frac_lopri);

      if (measured_ratio_over_intended_ratio <
          acceptable_measured_ratio_over_intended_ratio) {
        double hipri_usage_bps =
            8 * hipri_usage_bytes / absl::ToDoubleSeconds(time - cur_state.last_time);
        double lopri_usage_bps =
            8 * lopri_usage_bytes / absl::ToDoubleSeconds(time - cur_state.last_time);

        // Rate limiting is not perfect, avoid increasing the LOPRI limit.
        const int64_t new_lopri_limit =
            std::min<int64_t>(lopri_usage_bps, cur_state.alloc.lopri_rate_limit_bps());

        auto to_mbps = [](auto bps) { return static_cast<double>(bps) / (1024 * 1024); };

        SPDLOG_LOGGER_INFO(
            logger,
            "flow: {}: inferred congestion (ratio = {}): sent {} Mbps as HIPRI "
            "but only {} Mbps as LOPRI (lopri_frac = {})",
            parent.flow().ShortDebugString(), measured_ratio_over_intended_ratio,
            to_mbps(hipri_usage_bps), to_mbps(lopri_usage_bps), cur_state.frac_lopri);
        SPDLOG_LOGGER_INFO(
            logger, "flow: {}: old LOPRI limit: {} Mbps new LOPRI limit: {} Mbps",
            parent.flow().ShortDebugString(),
            to_mbps(cur_state.alloc.lopri_rate_limit_bps()), to_mbps(new_lopri_limit));

        return new_lopri_limit;
      }
    }
  }
  return cur_state.alloc.lopri_rate_limit_bps();
}

}  // namespace heyp

#endif  // HEYP_ALG_QOS_DOWNGRADE_H_
