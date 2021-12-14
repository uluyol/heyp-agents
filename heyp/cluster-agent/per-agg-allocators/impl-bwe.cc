#include "heyp/cluster-agent/per-agg-allocators/impl-bwe.h"

#include "heyp/alg/fairness/max-min-fairness.h"
#include "heyp/alg/rate-limits.h"

namespace heyp {

BweAggAllocator::BweAggAllocator(const proto::ClusterAllocatorConfig& config,
                                 ClusterFlowMap<proto::FlowAlloc> agg_admissions)
    : config_(config),
      agg_admissions_(std::move(agg_admissions)),
      logger_(MakeLogger("bwe-alloc")) {}

std::vector<proto::FlowAlloc> BweAggAllocator::AllocAgg(
    absl::Time time, const proto::AggInfo& agg_info,
    proto::DebugAllocRecord::DebugState* debug_state) {
  auto admissions_iter = agg_admissions_.find(agg_info.parent().flow());
  if (admissions_iter == agg_admissions_.end()) {
    SPDLOG_LOGGER_INFO(&logger_, "no admission for FG {}",
                       agg_info.parent().flow().ShortDebugString());
    return {};
  }

  const proto::FlowAlloc& admission = admissions_iter->second;

  H_SPDLOG_CHECK_EQ_MESG(&logger_, admission.lopri_rate_limit_bps(), 0,
                         "Bwe allocation incompatible with QoS downgrade");
  int64_t cluster_admission = admission.hipri_rate_limit_bps();

  *debug_state->mutable_parent_alloc() = admission;

  if (config_.enable_burstiness()) {
    double burstiness = BweBurstinessFactor(agg_info);
    cluster_admission = cluster_admission * burstiness;
    debug_state->set_burstiness(burstiness);
  } else {
    debug_state->set_burstiness(1);
  }

  std::vector<int64_t> demands;
  demands.reserve(agg_info.children_size());
  for (const proto::FlowInfo& info : agg_info.children()) {
    demands.push_back(info.predicted_demand_bps());
  }

  SingleLinkMaxMinFairnessProblem problem;
  int64_t waterlevel = problem.ComputeWaterlevel(cluster_admission, demands);

  int64_t bonus = 0;
  if (config_.enable_bonus()) {
    bonus = EvenlyDistributeExtra(cluster_admission, demands, waterlevel);
  }
  debug_state->set_hipri_bonus(bonus);

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

}  // namespace heyp
