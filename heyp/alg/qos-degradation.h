#ifndef HEYP_ALG_QOS_DEGRADATION_H_
#define HEYP_ALG_QOS_DEGRADATION_H_

#include <cstdint>
#include <vector>

#include "heyp/proto/heyp.pb.h"

namespace heyp {

std::vector<bool> HeypSigcomm20PickLOPRIChildren(const proto::AggInfo& agg_info,
                                                 const double want_frac_lopri);

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

}  // namespace heyp

#endif  // HEYP_ALG_QOS_DEGRADATION_H_
