#include "heyp/cluster-agent/allocator.h"

#include "absl/container/flat_hash_map.h"
#include "glog/logging.h"
#include "heyp/alg/rate-limits.h"
#include "heyp/proto/alg.h"
#include "heyp/threads/executor.h"
#include "routing-algos/alg/max-min-fairness.h"

namespace heyp {
namespace {

template <typename ValueType>
using FlowMap =
    absl::flat_hash_map<proto::FlowMarker, ValueType, HashFlow, EqFlow>;

FlowMap<proto::FlowAlloc> ToAdmissionsMap(
    const proto::AllocBundle& cluster_wide_allocs) {
  FlowMap<proto::FlowAlloc> map;
  for (const proto::FlowAlloc& a : cluster_wide_allocs.flow_allocs()) {
    map[a.flow()] = a;
  }
  return map;
}

using AllocFunc = std::function<std::vector<proto::FlowAlloc>(
    absl::Time time, const proto::FlowAlloc& admission,
    const proto::AggInfo& info)>;

constexpr int kNumAllocCores = 8;

class AsyncClusterAllocator : public ClusterAllocator {
 public:
  AsyncClusterAllocator(const AllocFunc& alloc_fn,
                        FlowMap<proto::FlowAlloc> cluster_admissions)
      : alloc_fn_(alloc_fn),
        cluster_admissions_(std::move(cluster_admissions)),
        exec_(kNumAllocCores) {}

  void Reset() override {
    absl::MutexLock l(&mu_);
    group_ = exec_.NewTaskGroup();
    allocs_.partial_sets.clear();
  }

  void AddInfo(absl::Time time, const proto::AggInfo& info) override {
    group_->AddTask([time, info, this] {
      auto a = this->alloc_fn_(
          time, cluster_admissions_.at(info.parent().flow()), info);
      absl::MutexLock l(&this->mu_);
      allocs_.partial_sets.push_back(std::move(a));
    });
  }

  AllocSet GetAllocs() override {
    group_->WaitAll();
    absl::MutexLock l(&mu_);
    return allocs_;
  }

 private:
  const AllocFunc alloc_fn_;
  const FlowMap<proto::FlowAlloc> cluster_admissions_;
  Executor exec_;

  std::unique_ptr<TaskGroup> group_;
  absl::Mutex mu_;
  AllocSet allocs_ ABSL_GUARDED_BY(mu_);
};

AllocFunc BweAllocFunc(const proto::ClusterAllocatorConfig& config) {
  return [config](absl::Time time, const proto::FlowAlloc& admission,
                  const proto::AggInfo& info) -> std::vector<proto::FlowAlloc> {
    CHECK_EQ(admission.lopri_rate_limit_bps(), 0)
        << "Bwe allocation incompatible with QoS degradation";
    int64_t cluster_admission = admission.hipri_rate_limit_bps();
    if (config.enable_burstiness()) {
      double burstiness = BweBurstinessFactor(info);
      cluster_admission = cluster_admission * burstiness;
    }

    std::vector<int64_t> demands;
    demands.reserve(info.children_size());
    for (const proto::FlowInfo& child_info : info.children()) {
      demands.push_back(child_info.predicted_demand_bps());
    }

    routing_algos::SingleLinkMaxMinFairnessProblem problem;
    int64_t waterlevel =
        problem.ComputeWaterlevel(cluster_admission, {demands});

    int64_t bonus = 0;
    if (config.enable_bonus()) {
      bonus = EvenlyDistributeExtra(cluster_admission, demands, waterlevel);
    }

    const int64_t limit = config.oversub_factor() * (waterlevel + bonus);

    std::vector<proto::FlowAlloc> allocs;
    allocs.reserve(info.children_size());
    for (const proto::FlowInfo& child_info : info.children()) {
      proto::FlowAlloc alloc;
      *alloc.mutable_flow() = child_info.flow();
      alloc.set_hipri_rate_limit_bps(limit);
      allocs.push_back(std::move(alloc));
    }
    return allocs;
  };
}

AllocFunc HeypSigcomm20AllocFunc(const proto::ClusterAllocatorConfig& config);

}  // namespace

std::unique_ptr<ClusterAllocator> CreateClusterAllocator(
    const proto::ClusterAllocatorConfig& config,
    const proto::AllocBundle& cluster_wide_allocs) {
  FlowMap<proto::FlowAlloc> cluster_admissions =
      ToAdmissionsMap(cluster_wide_allocs);
  switch (config.type()) {
    case proto::ClusterAllocatorType::BWE:
      return absl::make_unique<AsyncClusterAllocator>(
          BweAllocFunc(config), std::move(cluster_admissions));
    case proto::ClusterAllocatorType::HEYP_SIGCOMM20:
      return absl::make_unique<AsyncClusterAllocator>(
          HeypSigcomm20AllocFunc(config), std::move(cluster_admissions));
  }
  LOG(FATAL) << "unreachable: got cluster allocator type: " << config.type();
  return nullptr;
}

}  // namespace heyp
