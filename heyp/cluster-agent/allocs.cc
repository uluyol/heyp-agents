#include "heyp/cluster-agent/allocs.h"

namespace heyp {

absl::flat_hash_map<int64_t, proto::AllocBundle> BundleByHost(AllocSet allocs) {
  absl::flat_hash_map<int64_t, proto::AllocBundle> by_host;
  for (size_t i = 0; i < allocs.partial_sets.size(); ++i) {
    for (size_t j = 0; j < allocs.partial_sets[i].size(); ++j) {
      int64_t host_id = allocs.partial_sets[i][j].flow().host_id();
      by_host[host_id].mutable_flow_allocs()->Add(std::move(allocs.partial_sets[i][j]));
    }
  }
  return by_host;
}

}  // namespace heyp
