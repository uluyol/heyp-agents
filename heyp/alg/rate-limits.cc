#include "heyp/alg/rate-limits.h"

namespace heyp {

std::ostream& operator<<(std::ostream& os, const RateLimits& limits) {
  return os << "(" << limits.hipri_limit_bps << ", " << limits.lopri_limit_bps << ")";
}

double BweBurstinessFactor(const proto::AggInfo& info) {
  double parent_demand_bps = info.parent().predicted_demand_bps();
  double sum_child_demand_bps = 0;
  for (const proto::FlowInfo& c : info.children()) {
    sum_child_demand_bps += c.predicted_demand_bps();
  }

  if (parent_demand_bps == 0 || sum_child_demand_bps == 0) {
    return 1;
  }

  if (sum_child_demand_bps < parent_demand_bps) {
    // Due to the fact that we have multiple ways of measuring usage (one-shot average
    // over a window, EWMA based on multiple fine-grained values), it's possible that the
    // parent has higher demand that the sum of all children.
    //
    // Rather than try and change usage measurement and demand estimation to make this
    // impossible, just handle it here.
    return 1.0;
  }

  return sum_child_demand_bps / parent_demand_bps;
}

int64_t EvenlyDistributeExtra(int64_t admission, const std::vector<int64_t>& demands,
                              int64_t waterlevel) {
  if (demands.empty()) {
    return admission;
  }
  for (int64_t d : demands) {
    admission -= std::min(d, waterlevel);
  }
  admission = std::max<int64_t>(0, admission);
  return admission / demands.size();
}

}  // namespace heyp
