#include "heyp/alg/rate-limits.h"

#include "absl/base/macros.h"

namespace heyp {

std::ostream& operator<<(std::ostream& os, const RateLimits& limits) {
  return os << "(" << limits.hipri_limit_bps << ", " << limits.lopri_limit_bps
            << ")";
}

double BweBurstinessFactor(const ClusterStateSnapshot& s) {
  double parent_demand_bps = s.state.predicted_demand_bps;
  double sum_child_demand_bps = 0;
  for (const FlowStateSnapshot& cs : s.host_info) {
    sum_child_demand_bps += cs.predicted_demand_bps;
  }

  if (parent_demand_bps == 0) {
    ABSL_ASSERT(sum_child_demand_bps == 0);
    return 1;
  }

  double burstiness = sum_child_demand_bps / parent_demand_bps;
  ABSL_ASSERT(burstiness >= 1);
  return burstiness;
}

}  // namespace heyp
