#include "heyp/cluster-agent/per-agg-allocators/impl-fixed-host-pattern.h"

#include "heyp/alg/debug.h"

namespace heyp {
namespace {

class SnapshotHostIter {
  const proto::FixedClusterHostAllocs::Snapshot& snapshot_;
  int pair_index_ = 0;
  int pair_remaining_ = 0;
  const proto::FlowAlloc empty_alloc_;

 public:
  SnapshotHostIter(const proto::FixedClusterHostAllocs::Snapshot& snapshot)
      : snapshot_(snapshot) {
    if (snapshot_.host_allocs_size() > 0) {
      pair_remaining_ = snapshot_.host_allocs(0).num_hosts();
    }
  }

  const proto::FlowAlloc& Next() {
    while (pair_index_ < snapshot_.host_allocs_size()) {
      if (pair_remaining_ > 0) {
        --pair_remaining_;
        return snapshot_.host_allocs(pair_index_).alloc();
      }
      ++pair_index_;
      if (pair_index_ < snapshot_.host_allocs_size()) {
        pair_remaining_ = snapshot_.host_allocs(pair_index_).num_hosts();
      }
    }
    return empty_alloc_;
  }
};

}  // namespace

FixedHostPatternAllocator::FixedHostPatternAllocator(
    const proto::ClusterAllocatorConfig& config)
    : logger_(MakeLogger("host-pattern-alloc")), next_(0) {
  for (const proto::FixedClusterHostAllocs& p : config.fixed_host_alloc_patterns()) {
    alloc_patterns_[p.cluster()] = p;
  }
}

std::vector<proto::FlowAlloc> FixedHostPatternAllocator::AllocAgg(
    absl::Time time, const proto::AggInfo& agg_info,
    proto::DebugAllocRecord::DebugState* debug_state) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  auto alloc_pattern_iter = alloc_patterns_.find(agg_info.parent().flow());
  if (alloc_pattern_iter == alloc_patterns_.end()) {
    SPDLOG_LOGGER_INFO(&logger_, "no admission for FG {}",
                       agg_info.parent().flow().ShortDebugString());
    return {};
  }

  const proto::FixedClusterHostAllocs& alloc_pattern = alloc_pattern_iter->second;

  if (alloc_pattern.snapshots().empty()) {
    return {};
  }

  if (should_debug) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(&logger_, "allocating for time = {} step = {}", time, next_);
    }
  }

  const proto::FixedClusterHostAllocs::Snapshot& snapshot_pattern =
      alloc_pattern.snapshots(next_ % alloc_pattern.snapshots().size());
  next_++;

  SnapshotHostIter alloc_iter(snapshot_pattern);

  std::vector<proto::FlowAlloc> allocs;
  allocs.reserve(agg_info.children_size());
  for (size_t i = 0; i < agg_info.children_size(); ++i) {
    proto::FlowAlloc alloc = alloc_iter.Next();
    *alloc.mutable_flow() = agg_info.children(i).flow();
    allocs.push_back(std::move(alloc));
  }
  return allocs;
}

}  // namespace heyp
