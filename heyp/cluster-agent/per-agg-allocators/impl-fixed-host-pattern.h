#ifndef HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_FIXED_HOST_PATTERN_H_
#define HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_FIXED_HOST_PATTERN_H_

#include "heyp/cluster-agent/per-agg-allocators/interface.h"
#include "heyp/cluster-agent/per-agg-allocators/util.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/config.pb.h"

namespace heyp {

class FixedHostPatternAllocator : public PerAggAllocator {
 public:
  FixedHostPatternAllocator(const proto::ClusterAllocatorConfig& config);

  std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info,
      proto::DebugAllocRecord::DebugState* debug_state) override;

 private:
  spdlog::logger logger_;
  ClusterFlowMap<proto::FixedClusterHostAllocs> alloc_patterns_;
  size_t next_;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_FIXED_HOST_PATTERN_H_
