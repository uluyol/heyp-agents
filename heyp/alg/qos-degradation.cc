#include "heyp/alg/qos-degradation.h"

#include <algorithm>

namespace heyp {

template <bool StateToIncrease>
void GreedyAssignToMinimizeGap(GreedyAssignToMinimizeGapArgs args,
                               std::vector<bool>& lopri_children) {
  for (size_t child_i : args.children_sorted_by_dec_demand) {
    if (lopri_children[child_i] == StateToIncrease) {
      continue;  // child already belongs to our bin, don't flip
    }
    // Try to flip child_i to our bin.
    int64_t next_demand =
        args.cur_demand +
        args.agg_info.children(child_i).predicted_demand_bps();
    if (next_demand > args.want_demand) {
      continue;  // flipping child_i overshoots the goal
    }
    // Safe to flip child_i;
    lopri_children[child_i] = StateToIncrease;
    args.cur_demand = next_demand;
  }
}

// Instantiate both (true/false) variants
template void GreedyAssignToMinimizeGap<false>(
    GreedyAssignToMinimizeGapArgs args, std::vector<bool>& lopri_children);
template void GreedyAssignToMinimizeGap<true>(
    GreedyAssignToMinimizeGapArgs args, std::vector<bool>& lopri_children);

std::vector<bool> HeypSigcomm20PickLOPRIChildren(const proto::AggInfo& agg_info,
                                                 const double want_frac_lopri) {
  std::vector<bool> lopri_children(agg_info.children_size(), false);
  int64_t total_demand = 0;
  int64_t lopri_demand = 0;
  std::vector<size_t> children_sorted_by_dec_demand(agg_info.children_size(),
                                                    0);
  for (size_t i = 0; i < agg_info.children_size(); i++) {
    children_sorted_by_dec_demand[i] = i;
    const auto& c = agg_info.children(i);
    total_demand += c.predicted_demand_bps();
    if (c.currently_lopri()) {
      lopri_children[i] = true;
      lopri_demand += c.predicted_demand_bps();
    } else {
    }
  }

  if (total_demand == 0) {
    // Don't use LOPRI if all demand is zero.
    return std::vector<bool>(agg_info.children_size(), false);
  }

  std::sort(
      children_sorted_by_dec_demand.begin(),
      children_sorted_by_dec_demand.end(),
      [&agg_info](size_t lhs, size_t rhs) -> bool {
        int64_t lhs_demand = agg_info.children(lhs).predicted_demand_bps();
        int64_t rhs_demand = agg_info.children(rhs).predicted_demand_bps();
        if (lhs_demand == rhs_demand) {
          return lhs > rhs;
        }
        return lhs_demand > rhs_demand;
      });

  if (static_cast<double>(lopri_demand) / static_cast<double>(total_demand) >
      want_frac_lopri) {
    // Move from LOPRI to HIPRI
    int64_t hipri_demand = total_demand - lopri_demand;
    int64_t want_demand = (1 - want_frac_lopri) * total_demand;
    GreedyAssignToMinimizeGap<false>(
        {
            .cur_demand = hipri_demand,
            .want_demand = want_demand,
            .children_sorted_by_dec_demand = children_sorted_by_dec_demand,
            .agg_info = agg_info,
        },
        lopri_children);
  } else {
    // Move from HIPRI to LOPRI
    int64_t want_demand = want_frac_lopri * total_demand;
    GreedyAssignToMinimizeGap<true>(
        {
            .cur_demand = lopri_demand,
            .want_demand = want_demand,
            .children_sorted_by_dec_demand = children_sorted_by_dec_demand,
            .agg_info = agg_info,
        },
        lopri_children);
  }

  return lopri_children;
}

double FracAdmittedAtLOPRI(const proto::FlowInfo& parent,
                           const proto::FlowAlloc& cur_alloc) {
  if (parent.predicted_demand_bps() > cur_alloc.hipri_rate_limit_bps()) {
    const double total_rate_limit_bps =
        cur_alloc.hipri_rate_limit_bps() + cur_alloc.lopri_rate_limit_bps();
    const double total_admitted_demand_bps =
        std::min<double>(parent.predicted_demand_bps(), total_rate_limit_bps);
    return 1 - (cur_alloc.hipri_rate_limit_bps() / total_admitted_demand_bps);
  }
  return 0;
}

}  // namespace heyp
