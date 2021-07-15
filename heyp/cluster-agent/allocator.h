#ifndef HEYP_CLUSTER_AGENT_ALLOCATOR_H_
#define HEYP_CLUSTER_AGENT_ALLOCATOR_H_

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "heyp/cluster-agent/allocs.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/proto/ndjson-logger.h"
#include "heyp/threads/executor.h"

namespace heyp {

class PerAggAllocator;

class ClusterAllocator {
 public:
  static absl::StatusOr<std::unique_ptr<ClusterAllocator>> Create(
      const proto::ClusterAllocatorConfig& config,
      const proto::AllocBundle& cluster_wide_allocs, double demand_multiplier,
      NdjsonLogger* alloc_recorder = nullptr /* optional */);

  ~ClusterAllocator();

  void Reset();
  void AddInfo(absl::Time time, const proto::AggInfo& info);
  AllocSet GetAllocs();

 private:
  ClusterAllocator(std::unique_ptr<PerAggAllocator> alloc, NdjsonLogger* alloc_recorder);

  std::unique_ptr<PerAggAllocator> alloc_;
  Executor exec_;

  std::unique_ptr<TaskGroup> group_;
  absl::Mutex mu_;
  NdjsonLogger* alloc_recorder_ ABSL_GUARDED_BY(mu_);
  AllocSet allocs_ ABSL_GUARDED_BY(mu_);
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_ALLOCATOR_H_
