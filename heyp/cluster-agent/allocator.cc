#include "heyp/cluster-agent/allocator.h"

#include "absl/container/flat_hash_map.h"
#include "heyp/alg/qos-degradation.h"
#include "heyp/alg/rate-limits.h"
#include "heyp/proto/alg.h"
#include "routing-algos/alg/max-min-fairness.h"

namespace heyp {

class PerAggAllocator {
 public:
  virtual ~PerAggAllocator() = default;
  virtual std::vector<proto::FlowAlloc> AllocAgg(absl::Time time,
                                                 const proto::AggInfo& agg_info) = 0;
};

constexpr int kNumAllocCores = 8;

ClusterAllocator::ClusterAllocator(std::unique_ptr<PerAggAllocator> alloc,
                                   AllocRecorder* recorder)
    : alloc_(std::move(alloc)), exec_(kNumAllocCores), recorder_(recorder) {}

ClusterAllocator::~ClusterAllocator() {}

void ClusterAllocator::Reset() {
  absl::MutexLock l(&mu_);
  group_ = exec_.NewTaskGroup();
  allocs_.partial_sets.clear();
}

void ClusterAllocator::AddInfo(absl::Time time, const proto::AggInfo& info) {
  group_->AddTaskNoStatus([time, info, this] {
    auto a = this->alloc_->AllocAgg(time, info);
    absl::MutexLock l(&this->mu_);
    if (this->recorder_ != nullptr) {
      this->recorder_->Record(time, info, a);
    }
    allocs_.partial_sets.push_back(std::move(a));
  });
}

AllocSet ClusterAllocator::GetAllocs() {
  group_->WaitAllNoStatus();
  absl::MutexLock l(&mu_);
  return allocs_;
}

namespace {

template <typename ValueType>
using FlowMap = absl::flat_hash_map<proto::FlowMarker, ValueType, HashFlow, EqFlow>;

FlowMap<proto::FlowAlloc> ToAdmissionsMap(const proto::AllocBundle& cluster_wide_allocs) {
  FlowMap<proto::FlowAlloc> map;
  for (const proto::FlowAlloc& a : cluster_wide_allocs.flow_allocs()) {
    map[a.flow()] = a;
  }
  return map;
}
class BweAggAllocator : public PerAggAllocator {
 public:
  BweAggAllocator(const proto::ClusterAllocatorConfig& config,
                  FlowMap<proto::FlowAlloc> agg_admissions)
      : config_(config), agg_admissions_(std::move(agg_admissions)) {}

  std::vector<proto::FlowAlloc> AllocAgg(absl::Time time,
                                         const proto::AggInfo& agg_info) override {
    const proto::FlowAlloc& admission = agg_admissions_.at(agg_info.parent().flow());

    CHECK_EQ(admission.lopri_rate_limit_bps(), 0)
        << "Bwe allocation incompatible with QoS degradation";
    int64_t cluster_admission = admission.hipri_rate_limit_bps();
    if (config_.enable_burstiness()) {
      double burstiness = BweBurstinessFactor(agg_info);
      cluster_admission = cluster_admission * burstiness;
    }

    std::vector<int64_t> demands;
    demands.reserve(agg_info.children_size());
    for (const proto::FlowInfo& info : agg_info.children()) {
      demands.push_back(info.predicted_demand_bps());
    }

    routing_algos::SingleLinkMaxMinFairnessProblem problem;
    int64_t waterlevel = problem.ComputeWaterlevel(cluster_admission, {demands});

    int64_t bonus = 0;
    if (config_.enable_bonus()) {
      bonus = EvenlyDistributeExtra(cluster_admission, demands, waterlevel);
    }

    const int64_t limit = config_.oversub_factor() * (waterlevel + bonus);

    std::vector<proto::FlowAlloc> allocs;
    allocs.reserve(agg_info.children_size());
    for (const proto::FlowInfo& info : agg_info.children()) {
      proto::FlowAlloc alloc;
      *alloc.mutable_flow() = info.flow();
      alloc.set_hipri_rate_limit_bps(limit);
      allocs.push_back(std::move(alloc));
    }
    return allocs;
  }

 private:
  const proto::ClusterAllocatorConfig config_;
  const FlowMap<proto::FlowAlloc> agg_admissions_;
};

class HeypSigcomm20Allocator : public PerAggAllocator {
 private:
  struct PerAggState {
    proto::FlowAlloc alloc;
    double frac_lopri = 0;
    absl::Time last_time = absl::UnixEpoch();
    int64_t last_cum_hipri_usage_bytes = 0;
    int64_t last_cum_lopri_usage_bytes = 0;
  };

  const proto::ClusterAllocatorConfig config_;
  const double demand_multiplier_;
  FlowMap<PerAggState> agg_states_;

 public:
  HeypSigcomm20Allocator(const proto::ClusterAllocatorConfig& config,
                         FlowMap<proto::FlowAlloc> agg_admissions,
                         double demand_multiplier)
      : config_(config), demand_multiplier_(demand_multiplier) {
    for (const auto& flow_alloc_pair : agg_admissions) {
      agg_states_[flow_alloc_pair.first] = {.alloc = flow_alloc_pair.second};
    }
  }

  std::vector<proto::FlowAlloc> AllocAgg(absl::Time time,
                                         const proto::AggInfo& agg_info) override {
    PerAggState& cur_state = agg_states_.at(agg_info.parent().flow());
    cur_state.alloc.set_lopri_rate_limit_bps(HeypSigcomm20MaybeReviseLOPRIAdmission(
        config_.heyp_acceptable_measured_ratio_over_intended_ratio(), time,
        agg_info.parent(), cur_state));

    cur_state.last_time = time;
    cur_state.last_cum_hipri_usage_bytes = agg_info.parent().cum_hipri_usage_bytes();
    cur_state.last_cum_lopri_usage_bytes = agg_info.parent().cum_lopri_usage_bytes();

    int64_t hipri_admission = cur_state.alloc.hipri_rate_limit_bps();
    int64_t lopri_admission = cur_state.alloc.lopri_rate_limit_bps();

    cur_state.frac_lopri =
        FracAdmittedAtLOPRI(agg_info.parent(), hipri_admission, lopri_admission);
    if (config_.heyp_probe_lopri_when_ambiguous()) {
      ShouldProbeLOPRI(agg_info, hipri_admission, lopri_admission, demand_multiplier_,
                       &cur_state.frac_lopri);
    }

    ABSL_ASSERT(cur_state.frac_lopri >= 0);
    ABSL_ASSERT(cur_state.frac_lopri <= 1);

    // Burstiness matters for selecting children and assigning them rate limits.
    if (config_.enable_burstiness()) {
      double burstiness = BweBurstinessFactor(agg_info);
      hipri_admission = hipri_admission * burstiness;
      lopri_admission = lopri_admission * burstiness;
    }

    std::vector<bool> lopri_children =
        HeypSigcomm20PickLOPRIChildren(agg_info, cur_state.frac_lopri);

    std::vector<int64_t> hipri_demands;
    std::vector<int64_t> lopri_demands;
    hipri_demands.reserve(agg_info.children_size());
    lopri_demands.reserve(agg_info.children_size());
    for (size_t i = 0; i < agg_info.children_size(); ++i) {
      if (lopri_children[i]) {
        lopri_demands.push_back(agg_info.children(i).predicted_demand_bps());
      } else {
        hipri_demands.push_back(agg_info.children(i).predicted_demand_bps());
      }
    }

    routing_algos::SingleLinkMaxMinFairnessProblem problem;
    int64_t hipri_waterlevel =
        problem.ComputeWaterlevel(hipri_admission, {hipri_demands});
    int64_t lopri_waterlevel =
        problem.ComputeWaterlevel(lopri_admission, {lopri_demands});

    int64_t hipri_bonus = 0;
    int64_t lopri_bonus = 0;
    if (config_.enable_bonus()) {
      hipri_bonus =
          EvenlyDistributeExtra(hipri_admission, hipri_demands, hipri_waterlevel);
      lopri_bonus =
          EvenlyDistributeExtra(lopri_admission, lopri_demands, lopri_waterlevel);
    }

    const int64_t hipri_limit =
        config_.oversub_factor() * (hipri_waterlevel + hipri_bonus);
    const int64_t lopri_limit =
        config_.oversub_factor() * (lopri_waterlevel + lopri_bonus);

    std::vector<proto::FlowAlloc> allocs;
    allocs.reserve(agg_info.children_size());
    for (size_t i = 0; i < agg_info.children_size(); ++i) {
      proto::FlowAlloc alloc;
      *alloc.mutable_flow() = agg_info.children(i).flow();
      if (lopri_children[i]) {
        alloc.set_lopri_rate_limit_bps(lopri_limit);
      } else {
        alloc.set_hipri_rate_limit_bps(hipri_limit);
      }
      allocs.push_back(std::move(alloc));
    }
    return allocs;
  }
};

}  // namespace

std::unique_ptr<ClusterAllocator> ClusterAllocator::Create(
    const proto::ClusterAllocatorConfig& config,
    const proto::AllocBundle& cluster_wide_allocs, double demand_multiplier,
    AllocRecorder* recorder) {
  FlowMap<proto::FlowAlloc> cluster_admissions = ToAdmissionsMap(cluster_wide_allocs);
  switch (config.type()) {
    case proto::ClusterAllocatorType::BWE:
      return absl::WrapUnique(new ClusterAllocator(
          absl::make_unique<BweAggAllocator>(config, std::move(cluster_admissions)),
          recorder));
    case proto::ClusterAllocatorType::HEYP_SIGCOMM20:
      return absl::WrapUnique(new ClusterAllocator(
          absl::make_unique<HeypSigcomm20Allocator>(config, std::move(cluster_admissions),
                                                    demand_multiplier),
          recorder));
  }
  LOG(FATAL) << "unreachable: got cluster allocator type: " << config.type();
  return nullptr;
}

}  // namespace heyp
