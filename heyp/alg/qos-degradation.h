#ifndef HEYP_ALG_QOS_DEGRADATION_H_
#define HEYP_ALG_QOS_DEGRADATION_H_

#include <cstdint>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "glog/logging.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

std::vector<bool> HeypSigcomm20PickLOPRIChildren(const proto::AggInfo& agg_info,
                                                 const double want_frac_lopri);

double FracAdmittedAtLOPRI(const proto::FlowInfo& parent,
                           const proto::FlowAlloc& cur_alloc);

// --- Following are mainly exposed for unit testing ---

struct GreedyAssignToMinimizeGapArgs {
  int64_t cur_demand;
  const int64_t want_demand;
  const std::vector<size_t>& children_sorted_by_dec_demand;
  const proto::AggInfo& agg_info;
};

// GreedyAssignToMinimizeGap is a greedy algorithm to partition children into
// bins that have aggregate demand X and Y.
//
// - StateToIncrease specifies whether we need to increase HIPRI demand (false)
//   or LOPRI (true).
// - args.cur_demand is the sum of demands for children that currently belong to
//   the bin.
// - args.want_demand is the desired sum of demands for the bin.
template <bool StateToIncrease>
void GreedyAssignToMinimizeGap(GreedyAssignToMinimizeGapArgs args,
                               std::vector<bool>& lopri_children);

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
    const proto::FlowInfo& parent, const SingleAggState& cur_state) {
  if (cur_state.frac_lopri > 0) {
    const double hipri_usage_bytes =
        parent.cum_hipri_usage_bytes() - cur_state.last_cum_hipri_usage_bytes;
    const double lopri_usage_bytes =
        parent.cum_lopri_usage_bytes() - cur_state.last_cum_lopri_usage_bytes;

    if (hipri_usage_bytes == 0) {
      LOG(INFO) << absl::StrFormat("flow: %s: no HIPRI usage",
                                   parent.flow().ShortDebugString());
    } else {
      ABSL_ASSERT(hipri_usage_bytes > 0);
      const double measured_lopri_over_hipri =
          lopri_usage_bytes / hipri_usage_bytes;
      ABSL_ASSERT(cur_state.frac_lopri > 0);
      const double want_hipri_over_lopri =
          (1 - cur_state.frac_lopri) / cur_state.frac_lopri;

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
      const double measured_ratio_over_intended_ratio =
          measured_lopri_over_hipri * want_hipri_over_lopri;

      if (measured_ratio_over_intended_ratio <
          acceptable_measured_ratio_over_intended_ratio) {
        double hipri_usage_bps =
            8 * hipri_usage_bytes /
            absl::ToDoubleSeconds(time - cur_state.last_time);
        double lopri_usage_bps =
            8 * lopri_usage_bytes /
            absl::ToDoubleSeconds(time - cur_state.last_time);

        int64_t new_lopri_limit = hipri_usage_bps + lopri_usage_bps -
                                  cur_state.alloc.hipri_rate_limit_bps();
        // Rate limiting is not perfect, avoid increasing the LOPRI limit.
        new_lopri_limit =
            std::min(new_lopri_limit, cur_state.alloc.lopri_rate_limit_bps());

        auto to_mbps = [](auto bps) {
          return static_cast<double>(bps) / 1'000'000;
        };

        LOG(INFO) << absl::StrFormat(
            "flow: %s: inferred congestion (ratio = %f): sent %f Mbps as HIPRI but "
            "only %f "
            "Mbps as LOPRI ",
            parent.flow().ShortDebugString(), measured_ratio_over_intended_ratio, to_mbps(hipri_usage_bps),
            to_mbps(lopri_usage_bps));
        LOG(INFO) << absl::StrFormat(
            "flow: %s: old LOPRI limit: %f Mbps new LOPRI limit: %f Mbps",
            parent.flow().ShortDebugString(),
            to_mbps(cur_state.alloc.lopri_rate_limit_bps()),
            to_mbps(new_lopri_limit));

        return new_lopri_limit;
      }
    }
  }
  return cur_state.alloc.lopri_rate_limit_bps();
}

}  // namespace heyp

#endif  // HEYP_ALG_QOS_DEGRADATION_H_
