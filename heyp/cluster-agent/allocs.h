#ifndef HEYP_CLUSTER_AGENT_GROUP_BY_HOST_H_
#define HEYP_CLUSTER_AGENT_GROUP_BY_HOST_H_

#include <cstdint>
#include <ostream>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

struct AllocSet {
  std::vector<std::vector<proto::FlowAlloc>> partial_sets;
};

std::ostream& operator<<(std::ostream& os, const AllocSet& allocs);

absl::flat_hash_map<int64_t, proto::AllocBundle> BundleByHost(AllocSet allocs);

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_GROUP_BY_HOST_H_
