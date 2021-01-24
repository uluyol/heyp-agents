#include "heyp/alg/qos-degradation.h"

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

}  // namespace heyp
