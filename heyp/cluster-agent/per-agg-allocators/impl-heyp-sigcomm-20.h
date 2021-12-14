#ifndef HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_HEYP_SIGCOMM_20_H_
#define HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_HEYP_SIGCOMM_20_H_

#include "heyp/alg/qos-downgrade.h"
#include "heyp/cluster-agent/per-agg-allocators/interface.h"
#include "heyp/cluster-agent/per-agg-allocators/util.h"
#include "heyp/proto/config.pb.h"

namespace heyp {

class HeypSigcomm20Allocator : public PerAggAllocator {
 public:
  HeypSigcomm20Allocator(const proto::ClusterAllocatorConfig& config,
                         ClusterFlowMap<proto::FlowAlloc> agg_admissions,
                         double demand_multiplier);

  std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info,
      proto::DebugAllocRecord::DebugState* debug_state) override;

 private:
  struct PerAggState {
    proto::FlowAlloc alloc;
    double frac_lopri = 0;
    double frac_lopri_with_probing = 0;
    absl::Time last_time = absl::UnixEpoch();
    int64_t last_cum_hipri_usage_bytes = 0;
    int64_t last_cum_lopri_usage_bytes = 0;
  };

  const proto::ClusterAllocatorConfig config_;
  const double demand_multiplier_;
  spdlog::logger logger_;
  ClusterFlowMap<PerAggState> agg_states_;
  DowngradeSelector downgrade_selector_;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_HEYP_SIGCOMM_20_H_
