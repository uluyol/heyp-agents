#ifndef HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_INTERFACE_H_
#define HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_INTERFACE_H_

#include <vector>

#include "absl/time/time.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/proto/monitoring.pb.h"

namespace heyp {

class PerAggAllocator {
 public:
  virtual ~PerAggAllocator() = default;
  virtual std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info,
      proto::DebugAllocRecord::DebugState* debug_state) = 0;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_INTERFACE_H_
