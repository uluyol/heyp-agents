#ifndef HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_BWE_H_
#define HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_BWE_H_

#include "heyp/cluster-agent/per-agg-allocators/interface.h"
#include "heyp/cluster-agent/per-agg-allocators/util.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/config.pb.h"

namespace heyp {

class BweAggAllocator : public PerAggAllocator {
 public:
  BweAggAllocator(const proto::ClusterAllocatorConfig& config,
                  ClusterFlowMap<proto::FlowAlloc> agg_admissions);

  std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info,
      proto::DebugAllocRecord::DebugState* debug_state) override;

 private:
  const proto::ClusterAllocatorConfig config_;
  const ClusterFlowMap<proto::FlowAlloc> agg_admissions_;
  spdlog::logger logger_;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_BWE_H_
