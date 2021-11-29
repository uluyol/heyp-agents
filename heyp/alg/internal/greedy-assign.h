#ifndef HEYP_ALG_INTERNAL_GREEDY_ASSIGN_H_
#define HEYP_ALG_INTERNAL_GREEDY_ASSIGN_H_

#include <cstdbool>
#include <cstdint>
#include <vector>

#include "heyp/alg/agg-info-views.h"

namespace heyp {
namespace internal {

struct GreedyAssignToMinimizeGapArgs {
  int64_t cur_demand;
  const int64_t want_demand;
  const std::vector<size_t>& children_sorted_by_dec_demand;
  const AggInfoView& agg_info;
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
                               std::vector<bool>& lopri_children,
                               bool punish_only_largest) {
  for (size_t i = 0; i < args.children_sorted_by_dec_demand.size(); ++i) {
    const size_t child_i = args.children_sorted_by_dec_demand[i];
    if (lopri_children[child_i] == StateToIncrease) {
      continue;  // child already belongs to our bin, don't flip
    }
    // Try to flip child_i to our bin.
    int64_t next_demand = args.cur_demand + args.agg_info.children()[child_i].volume_bps;

    if (next_demand > args.want_demand) {
      bool exceeds_twice_gap = next_demand > 2 * args.want_demand - args.cur_demand;

      if (punish_only_largest) {
        if (!exceeds_twice_gap) {
          // safe to flip
          lopri_children[child_i] = StateToIncrease;
          args.cur_demand = next_demand;
        }
        return;
      }

      // Don't flip child_i if there are more children with smaller demands to flip.
      bool have_children_with_less_demand =
          i < args.children_sorted_by_dec_demand.size() - 1;

      if (have_children_with_less_demand || exceeds_twice_gap) {
        continue;  // flipping child_i overshoots the goal
      }
    }
    // Safe to flip child_i;
    lopri_children[child_i] = StateToIncrease;
    args.cur_demand = next_demand;
  }
}

}  // namespace internal
}  // namespace heyp

#endif  // HEYP_ALG_INTERNAL_GREEDY_ASSIGN_H_
