#include "heyp/cluster-agent/per-agg-allocators/impl-heyp-sigcomm-20.h"

#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/fairness/max-min-fairness.h"
#include "heyp/alg/rate-limits.h"

namespace heyp {

HeypSigcomm20Allocator::HeypSigcomm20Allocator(
    const proto::ClusterAllocatorConfig& config, FlowMap<proto::FlowAlloc> agg_admissions,
    double demand_multiplier)
    : config_(config),
      demand_multiplier_(demand_multiplier),
      logger_(MakeLogger("heyp-sigcomm20-alloc")),
      downgrade_selector_(config_.downgrade_selector()) {
  for (const auto& flow_alloc_pair : agg_admissions) {
    agg_states_[flow_alloc_pair.first] = {.alloc = flow_alloc_pair.second};
  }
}

std::vector<proto::FlowAlloc> HeypSigcomm20Allocator::AllocAgg(
    absl::Time time, const proto::AggInfo& agg_info,
    proto::DebugAllocRecord::DebugState* debug_state) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  auto state_iter = agg_states_.find(agg_info.parent().flow());
  if (state_iter == agg_states_.end()) {
    SPDLOG_LOGGER_INFO(&logger_, "no admission for FG {}",
                       agg_info.parent().flow().ShortDebugString());
    return {};
  }

  PerAggState& cur_state = state_iter->second;

  cur_state.alloc.set_lopri_rate_limit_bps(HeypSigcomm20MaybeReviseLOPRIAdmission(
      config_.heyp_acceptable_measured_ratio_over_intended_ratio(), time,
      agg_info.parent(), cur_state, &logger_));

  cur_state.last_time = time;
  cur_state.last_cum_hipri_usage_bytes = agg_info.parent().cum_hipri_usage_bytes();
  cur_state.last_cum_lopri_usage_bytes = agg_info.parent().cum_lopri_usage_bytes();

  int64_t hipri_admission = cur_state.alloc.hipri_rate_limit_bps();
  int64_t lopri_admission = cur_state.alloc.lopri_rate_limit_bps();

  if (should_debug) {
    SPDLOG_LOGGER_INFO(&logger_, "allocating for time = {}", time);
    SPDLOG_LOGGER_INFO(&logger_, "hipri admission = {} lopri admission = {}",
                       hipri_admission, lopri_admission);
  }

  *debug_state->mutable_parent_alloc() = cur_state.alloc;

  cur_state.frac_lopri = FracAdmittedAtLOPRI<FVSource::kPredictedDemand>(
      agg_info.parent(), hipri_admission, lopri_admission);
  if (config_.heyp_probe_lopri_when_ambiguous()) {
    cur_state.frac_lopri_with_probing =
        FracAdmittedAtLOPRIToProbe<FVSource::kPredictedDemand>(
            agg_info, hipri_admission, lopri_admission, demand_multiplier_,
            cur_state.frac_lopri, &logger_);
  } else {
    cur_state.frac_lopri_with_probing = cur_state.frac_lopri;
  }

  debug_state->set_frac_lopri_initial(cur_state.frac_lopri);
  debug_state->set_frac_lopri_with_probing(cur_state.frac_lopri_with_probing);

  if (should_debug) {
    SPDLOG_LOGGER_INFO(&logger_, "lopri_frac = {} lopri_frac_with_debugging = {}",
                       cur_state.frac_lopri, cur_state.frac_lopri_with_probing);
  }

  cur_state.frac_lopri_with_probing =
      ClampFracLOPRI(&logger_, cur_state.frac_lopri_with_probing);

  // Burstiness matters for selecting children and assigning them rate limits.
  if (config_.enable_burstiness()) {
    double burstiness = BweBurstinessFactor(agg_info);
    if (should_debug) {
      SPDLOG_LOGGER_INFO(&logger_, "burstiness factor = {}", burstiness);
    }
    hipri_admission = hipri_admission * burstiness;
    lopri_admission = lopri_admission * burstiness;

    debug_state->set_burstiness(burstiness);
  } else {
    debug_state->set_burstiness(1);
  }

  std::vector<bool> lopri_children =
      downgrade_selector_.PickLOPRIChildren(agg_info, cur_state.frac_lopri_with_probing);

  std::vector<int64_t> hipri_demands;
  std::vector<int64_t> lopri_demands;
  hipri_demands.reserve(agg_info.children_size());
  lopri_demands.reserve(agg_info.children_size());
  double sum_hipri_demand = 0;
  double sum_lopri_demand = 0;
  for (size_t i = 0; i < agg_info.children_size(); ++i) {
    if (lopri_children[i]) {
      lopri_demands.push_back(agg_info.children(i).predicted_demand_bps());
      sum_lopri_demand += lopri_demands.back();
    } else {
      hipri_demands.push_back(agg_info.children(i).predicted_demand_bps());
      sum_hipri_demand += hipri_demands.back();
    }
  }

  double frac_lopri_post_partition =
      sum_lopri_demand / (sum_hipri_demand + sum_lopri_demand);
  if (frac_lopri_post_partition < cur_state.frac_lopri) {
    // We may not send as much demand using LOPRI as we'd like because we weren't able
    // to partition children in the correct proportion.
    //
    // To avoid inferring congestion when there is none, record the actual fraction of
    // demand using LOPRI as frac_lopri when it is lower.
    cur_state.frac_lopri = frac_lopri_post_partition;
  }

  debug_state->set_frac_lopri_post_partition(frac_lopri_post_partition);
  debug_state->set_frac_lopri_final(cur_state.frac_lopri);

  SingleLinkMaxMinFairnessProblem problem;
  int64_t hipri_waterlevel = problem.ComputeWaterlevel(hipri_admission, hipri_demands);
  int64_t lopri_waterlevel = problem.ComputeWaterlevel(lopri_admission, lopri_demands);

  if (should_debug) {
    SPDLOG_LOGGER_INFO(&logger_, "hipri waterlevel = {} lopri waterlevel = {}",
                       hipri_waterlevel, lopri_waterlevel);
  }
  int64_t hipri_bonus = 0;
  int64_t lopri_bonus = 0;
  if (config_.enable_bonus()) {
    hipri_bonus = EvenlyDistributeExtra(hipri_admission, hipri_demands, hipri_waterlevel);
    lopri_bonus = EvenlyDistributeExtra(lopri_admission, lopri_demands, lopri_waterlevel);
    if (should_debug) {
      SPDLOG_LOGGER_INFO(
          &logger_, "lopri admission = {} demands = [{}] lopri waterlevel = {}",
          lopri_admission, absl::StrJoin(lopri_demands, " "), lopri_waterlevel);
      SPDLOG_LOGGER_INFO(&logger_, "hipri bonus = {} lopri bonus = {}", hipri_bonus,
                         lopri_bonus);
    }
  }

  debug_state->set_hipri_bonus(hipri_bonus);
  debug_state->set_lopri_bonus(lopri_bonus);

  const int64_t hipri_limit = config_.oversub_factor() * (hipri_waterlevel + hipri_bonus);
  const int64_t lopri_limit = config_.oversub_factor() * (lopri_waterlevel + lopri_bonus);

  if (should_debug) {
    SPDLOG_LOGGER_INFO(&logger_, "hipri limit = {} lopri limit = {}", hipri_limit,
                       lopri_limit);
  }

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

}  // namespace heyp
