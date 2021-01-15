#ifndef HEYP_FLOWS_CLUSTER_STATE_H_
#define HEYP_FLOWS_CLUSTER_STATE_H_

#include <cstdint>
#include <vector>

#include "heyp/flows/state.h"

namespace heyp {

struct ClusterStateSnapshot {
  // state has all demand marked as hipri.
  FlowStateSnapshot state;
  int64_t cum_hipri_usage_bytes;
  int64_t cum_lopri_usage_bytes;
  std::vector<FlowStateSnapshot> host_info;
};

}  // namespace heyp

#endif  // HEYP_FLOWS_CLUSTER_STATE_H_
