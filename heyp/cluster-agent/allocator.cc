#include "heyp/cluster-agent/allocator.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_format.h"
#include "glog/logging.h"
#include "heyp/alg/qos-degradation.h"
#include "heyp/alg/rate-limits.h"
#include "heyp/proto/alg.h"
#include "routing-algos/alg/max-min-fairness.h"

namespace heyp {

class PerAggAllocator {
 public:
  virtual ~PerAggAllocator() = default;
  virtual std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info) = 0;
};

constexpr int kNumAllocCores = 8;

ClusterAllocator::ClusterAllocator(std::unique_ptr<PerAggAllocator> alloc)
    : alloc_(std::move(alloc)), exec_(kNumAllocCores) {}

void ClusterAllocator::Reset() {
  absl::MutexLock l(&mu_);
  group_ = exec_.NewTaskGroup();
  allocs_.partial_sets.clear();
}

void ClusterAllocator::AddInfo(absl::Time time, const proto::AggInfo& info) {
  group_->AddTask([time, info, this] {
    auto a = this->alloc_->AllocAgg(time, info);
    absl::MutexLock l(&this->mu_);
    allocs_.partial_sets.push_back(std::move(a));
  });
}

AllocSet ClusterAllocator::GetAllocs() {
  group_->WaitAll();
  absl::MutexLock l(&mu_);
  return allocs_;
}

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
class BweAggAllocator : public PerAggAllocator {
 public:
  BweAggAllocator(const proto::ClusterAllocatorConfig& config,
                  FlowMap<proto::FlowAlloc> agg_admissions)
      : config_(config), agg_admissions_(std::move(agg_admissions)) {}

  std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info) override {
    const proto::FlowAlloc& admission =
        agg_admissions_.at(agg_info.parent().flow());

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
    int64_t waterlevel =
        problem.ComputeWaterlevel(cluster_admission, {demands});

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

    // Used and maintained by MaybeReviseAdmission.
    absl::Time last_time = absl::UnixEpoch();
    int64_t last_cum_hipri_usage_bytes = 0;
    int64_t last_cum_lopri_usage_bytes = 0;
  };

  const proto::ClusterAllocatorConfig config_;
  FlowMap<PerAggState> agg_states_;

 public:
  HeypSigcomm20Allocator(const proto::ClusterAllocatorConfig& config,
                         FlowMap<proto::FlowAlloc> agg_admissions)
      : config_(config) {
    for (const auto& flow_alloc_pair : agg_admissions) {
      agg_states_[flow_alloc_pair.first] = {.alloc = flow_alloc_pair.second};
    }
  }

  // TODO: extract into heyp/alg and test.
  // Probably need to use templates to make generic over PerAggState types.
  void MaybeReviseAdmission(absl::Time time, const proto::FlowInfo& parent,
                            PerAggState& cur_state) {
    if (cur_state.frac_lopri > 0) {
      const double hipri_usage_bytes =
          parent.cum_hipri_usage_bytes() - cur_state.last_cum_hipri_usage_bytes;
      const double lopri_usage_bytes =
          parent.cum_lopri_usage_bytes() - cur_state.last_cum_lopri_usage_bytes;

      if (hipri_usage_bytes == 0) {
        LOG(INFO) << absl::StrFormat("flow: %s: no HIPRI usage",
                                     parent.flow().ShortDebugString());
      } else {
        ABSL_ASSERT(hipri_usage_bytes > 0);
        const double measured_lopri_over_hipri =
            lopri_usage_bytes / hipri_usage_bytes;
        ABSL_ASSERT(cur_state.frac_lopri > 0);
        const double want_hipri_over_lopri =
            (1 - cur_state.frac_lopri) / cur_state.frac_lopri;

        // Now, if we try to send X Gbps as LOPRI, but only succeed at sending
        // 0.8 * X Gbps as LOPRI, this indicates that we have some congestion on
        // LOPRI. Therefore, we should lower the LOPRI rate limit to mitigate
        // the congestion.
        //
        // On the other hand, if we try to send X Gbps as LOPRI but end up
        // sending more, this indicates that we have underestimated the demand
        // and marked a smaller portion of traffic with LOPRI than we should
        // have. It says nothing about LOPRI or HIPRI being congested, so leave
        // the rate limits alone.
        const double measured_ratio_over_intended_ratio =
            measured_lopri_over_hipri * want_hipri_over_lopri;

        if (measured_ratio_over_intended_ratio <
            config_.heyp_acceptable_measured_ratio_over_intended_ratio()) {
          double hipri_usage_bps =
              8 * hipri_usage_bytes /
              absl::ToDoubleSeconds(time - cur_state.last_time);
          double lopri_usage_bps =
              8 * lopri_usage_bytes /
              absl::ToDoubleSeconds(time - cur_state.last_time);

          int64_t new_lopri_limit = hipri_usage_bps + lopri_usage_bps -
                                    cur_state.alloc.hipri_rate_limit_bps();
          // Rate limiting is not perfect, avoid increasing the LOPRI limit.
          new_lopri_limit =
              std::min(new_lopri_limit, cur_state.alloc.lopri_rate_limit_bps());

          LOG(INFO) << absl::StrFormat(
              "flow: %s: inferred congestion: sent %f Mbps as HIPRI but "
              "only %f "
              "Mbps as LOPRI ",
              parent.flow().ShortDebugString(), hipri_usage_bps / 1'000'000,
              lopri_usage_bps / 1'000'000);
          LOG(INFO) << absl::StrFormat(
              "flow: %s: old LOPRI limit: %f Mbps new LOPRI limit: %f Mbps",
              parent.flow().ShortDebugString(),
              cur_state.alloc.lopri_rate_limit_bps() / 1'000'000,
              new_lopri_limit / 1'000'000);
          cur_state.alloc.set_lopri_rate_limit_bps(new_lopri_limit);
        }
      }
    }

    cur_state.last_time = time;
    cur_state.last_cum_hipri_usage_bytes = parent.cum_hipri_usage_bytes();
    cur_state.last_cum_lopri_usage_bytes = parent.cum_lopri_usage_bytes();
  }

  std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info) override {
    PerAggState& cur_state = agg_states_.at(agg_info.parent().flow());
    MaybeReviseAdmission(time, agg_info.parent(), cur_state);
    cur_state.frac_lopri =
        FracAdmittedAtLOPRI(agg_info.parent(), cur_state.alloc);
    std::vector<bool> lopri_children =
        HeypSigcomm20PickLOPRIChildren(agg_info, cur_state.frac_lopri);

    int64_t hipri_admission = cur_state.alloc.hipri_rate_limit_bps();
    int64_t lopri_admission = cur_state.alloc.lopri_rate_limit_bps();
    if (config_.enable_burstiness()) {
      double burstiness = BweBurstinessFactor(agg_info);
      hipri_admission = hipri_admission * burstiness;
      lopri_admission = lopri_admission * burstiness;
    }

    std::vector<int64_t> hipri_demands;
    std::vector<int64_t> lopri_demands;
    hipri_demands.reserve(agg_info.children_size());
    lopri_demands.reserve(agg_info.children_size());
    for (size_t i = 0; i < agg_info.children_size(); i++) {
      if (lopri_children[i]) {
        lopri_demands.push_back(agg_info.children(i).predicted_demand_bps());
      } else {
        hipri_demands.push_back(agg_info.children(i).predicted_demand_bps());
      }
    }

    routing_algos::SingleLinkMaxMinFairnessProblem problem;
    int64_t hipri_waterlevel =
        problem.ComputeWaterlevel(hipri_admission, {lopri_demands});
    int64_t lopri_waterlevel =
        problem.ComputeWaterlevel(lopri_admission, {lopri_demands});

    int64_t hipri_bonus = 0;
    int64_t lopri_bonus = 0;
    if (config_.enable_bonus()) {
      hipri_bonus = EvenlyDistributeExtra(hipri_admission, hipri_demands,
                                          hipri_waterlevel);
      lopri_bonus = EvenlyDistributeExtra(lopri_admission, lopri_demands,
                                          lopri_waterlevel);
    }

    const int64_t hipri_limit =
        config_.oversub_factor() * (hipri_waterlevel + hipri_bonus);
    const int64_t lopri_limit =
        config_.oversub_factor() * (lopri_waterlevel + lopri_bonus);

    std::vector<proto::FlowAlloc> allocs;
    allocs.reserve(agg_info.children_size());
    for (size_t i = 0; i < agg_info.children_size(); i++) {
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
    const proto::AllocBundle& cluster_wide_allocs) {
  FlowMap<proto::FlowAlloc> cluster_admissions =
      ToAdmissionsMap(cluster_wide_allocs);
  switch (config.type()) {
    case proto::ClusterAllocatorType::BWE:
      return absl::WrapUnique(
          new ClusterAllocator(absl::make_unique<BweAggAllocator>(
              config, std::move(cluster_admissions))));
    case proto::ClusterAllocatorType::HEYP_SIGCOMM20:
      return absl::WrapUnique(
          new ClusterAllocator(absl::make_unique<HeypSigcomm20Allocator>(
              config, std::move(cluster_admissions))));
  }
  LOG(FATAL) << "unreachable: got cluster allocator type: " << config.type();
  return nullptr;
}

}  // namespace heyp
