#include "heyp/cluster-agent/per-agg-allocators/util.h"

namespace heyp {

ClusterFlowMap<proto::FlowAlloc> ToAdmissionsMap(
    const proto::AllocBundle& cluster_wide_allocs) {
  ClusterFlowMap<proto::FlowAlloc> map;
  for (const proto::FlowAlloc& a : cluster_wide_allocs.flow_allocs()) {
    map[a.flow()] = a;
  }
  return map;
}

ClusterFlowMap<DowngradeSelector> MakeAggDowngradeSelectors(
    const proto::DowngradeSelector& selector,
    const ClusterFlowMap<proto::FlowAlloc>& admissions) {
  ClusterFlowMap<DowngradeSelector> ret;
  for (auto& [flow, alloc] : admissions) {
    ret.emplace(std::pair<proto::FlowMarker, DowngradeSelector>(
        flow, DowngradeSelector(selector)));
  }
  return ret;
}

}  // namespace heyp