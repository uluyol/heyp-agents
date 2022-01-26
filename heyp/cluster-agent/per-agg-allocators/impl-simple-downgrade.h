#ifndef HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_SIMPLE_DOWNGRADE_H_
#define HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_SIMPLE_DOWNGRADE_H_

#include "heyp/alg/qos-downgrade.h"
#include "heyp/cluster-agent/per-agg-allocators/interface.h"
#include "heyp/cluster-agent/per-agg-allocators/util.h"
#include "heyp/proto/config.pb.h"

namespace heyp {

class SimpleDowngradeAllocator : public PerAggAllocator {
 public:
  SimpleDowngradeAllocator(const proto::ClusterAllocatorConfig& config,
                           ClusterFlowMap<proto::FlowAlloc> agg_admissions,
                           double demand_multiplier);

  std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info,
      proto::DebugAllocRecord::DebugState* debug_state) override;

 private:
  struct PerAggState {
    double downgrade_frac = 0;
    double ewma_max_child_usage = -1;
    std::optional<DowngradeFracController> frac_controller;
    DowngradeSelector selector;
  };

  const proto::ClusterAllocatorConfig config_;
  const ClusterFlowMap<proto::FlowAlloc> agg_admissions_;
  spdlog::logger logger_;
  ClusterFlowMap<PerAggState> agg_states_;
  const FVSource downgrade_fv_source_;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_SIMPLE_DOWNGRADE_H_
