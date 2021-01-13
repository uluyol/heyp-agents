#ifndef HEYP_FLOWS_CLUSTER_FG_STATE_H_
#define HEYP_FLOWS_CLUSTER_FG_STATE_H_

#include <cstdint>
#include <vector>

#include "heyp/flows/state.h"

namespace heyp {

struct ClusterFGState {
  // state has all demand marked as hipri.
  FlowState state;
  int64_t cum_hipri_usage_bytes;
  int64_t cum_lopri_usage_bytes;
  std::vector<FlowState> host_info;
};

}  // namespace heyp

#endif  // HEYP_FLOWS_CLUSTER_FG_STATE_H_
