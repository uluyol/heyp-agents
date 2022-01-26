#ifndef HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_UTIL_H_
#define HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_UTIL_H_

#include "absl/container/flat_hash_map.h"
#include "heyp/alg/qos-downgrade.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/alg.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

double ClampFracLOPRI(spdlog::logger* logger, double frac_lopri);
double ClampFracLOPRISilent(double frac_lopri);

template <typename ValueType>
using ClusterFlowMap =
    absl::flat_hash_map<proto::FlowMarker, ValueType, HashClusterFlow, EqClusterFlow>;

ClusterFlowMap<proto::FlowAlloc> ToAdmissionsMap(
    const proto::AllocBundle& cluster_wide_allocs);

ClusterFlowMap<DowngradeSelector> MakeAggDowngradeSelectors(
    const proto::DowngradeSelector& selector,
    const ClusterFlowMap<proto::FlowAlloc>& admissions);

// Implementation //

inline double ClampFracLOPRI(spdlog::logger* logger, double frac_lopri) {
  // Make sure frac_lopri >= 0. Use a double-negative condition to
  // also capture the case where frac_lopri is NaN.
  if (!(frac_lopri >= 0.0)) {
    SPDLOG_LOGGER_WARN(logger, "frac_lopri [{}] < 0; clamping to 0", frac_lopri);
    return 0;
  }
  if (!(frac_lopri <= 1)) {
    SPDLOG_LOGGER_WARN(logger, "frac_lopri [{}] > 1; clamping to 1", frac_lopri);
    return 1;
  }
  return frac_lopri;
}

inline double ClampFracLOPRISilent(double frac_lopri) {
  // Make sure frac_lopri >= 0. Use a double-negative condition to
  // also capture the case where frac_lopri is NaN.
  if (!(frac_lopri >= 0.0)) {
    return 0;
  }
  if (!(frac_lopri <= 1)) {
    return 1;
  }
  return frac_lopri;
}

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_UTIL_H_
