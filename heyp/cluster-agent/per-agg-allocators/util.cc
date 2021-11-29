#include "heyp/cluster-agent/per-agg-allocators/util.h"

namespace heyp {

FlowMap<proto::FlowAlloc> ToAdmissionsMap(const proto::AllocBundle& cluster_wide_allocs) {
  FlowMap<proto::FlowAlloc> map;
  for (const proto::FlowAlloc& a : cluster_wide_allocs.flow_allocs()) {
    map[a.flow()] = a;
  }
  return map;
}

}  // namespace heyp