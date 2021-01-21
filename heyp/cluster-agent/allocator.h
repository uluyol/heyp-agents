#ifndef HEYP_CLUSTER_AGENT_ALLOCATOR_H_
#define HEYP_CLUSTER_AGENT_ALLOCATOR_H_

#include <memory>
#include <vector>

#include "absl/time/time.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

struct AllocSet {
  std::vector<std::vector<proto::FlowAlloc>> partial_sets;
};

class ClusterAllocator {
 public:
  virtual ~ClusterAllocator() = default;

  virtual void Reset() = 0;
  virtual void AddInfo(absl::Time time, const proto::AggInfo& info) = 0;
  virtual AllocSet GetAllocs() = 0;
};

std::unique_ptr<ClusterAllocator> CreateClusterAllocator(
    const proto::ClusterAllocatorConfig& config,
    const proto::AllocBundle& cluster_wide_allocs);

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_ALLOCATOR_H_
