#include "heyp/alg/rate-limits.h"

#include "absl/base/macros.h"

namespace heyp {

std::ostream& operator<<(std::ostream& os, const RateLimits& limits) {
  return os << "(" << limits.hipri_limit_bps << ", " << limits.lopri_limit_bps
            << ")";
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

  double burstiness = sum_child_demand_bps / parent_demand_bps;
  ABSL_ASSERT(burstiness >= 1);
  return burstiness;
}

int64_t EvenlyDistributeExtra(int64_t admission,
                              const std::vector<int64_t>& demands,
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
