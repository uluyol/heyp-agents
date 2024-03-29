#include "heyp/cluster-agent/allocs.h"

namespace heyp {

absl::flat_hash_map<uint64_t, proto::AllocBundle> BundleByHost(AllocSet allocs) {
  absl::flat_hash_map<uint64_t, proto::AllocBundle> by_host;
  for (size_t i = 0; i < allocs.partial_sets.size(); ++i) {
    for (size_t j = 0; j < allocs.partial_sets[i].size(); ++j) {
      uint64_t host_id = allocs.partial_sets[i][j].flow().host_id();
      by_host[host_id].mutable_flow_allocs()->Add(std::move(allocs.partial_sets[i][j]));
    }
  }
  return by_host;
}

std::ostream& operator<<(std::ostream& os, const AllocSet& allocs) {
  os << "AllocSet:\n";
  bool first_set = true;
  for (const std::vector<proto::FlowAlloc>& alloc_set : allocs.partial_sets) {
    if (!first_set) {
      os << "===========================================\n";
    }
    first_set = false;
    bool first_alloc = true;
    for (const proto::FlowAlloc& alloc : alloc_set) {
      if (!first_alloc) {
        os << "---------------------\n";
      }
      first_alloc = false;
      os << alloc.DebugString();
    }
  }
  return os;
}

}  // namespace heyp
