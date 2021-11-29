#include "heyp/cluster-agent/per-agg-allocators/impl-simple-downgrade.h"

#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/fairness/max-min-fairness.h"
#include "heyp/alg/rate-limits.h"

namespace heyp {
namespace {
constexpr int64_t kMaxChildBandwidthBps =
    100 * (static_cast<int64_t>(1) << 30);  // 100 Gbps
}

SimpleDowngradeAllocator::SimpleDowngradeAllocator(
    const proto::ClusterAllocatorConfig& config, FlowMap<proto::FlowAlloc> agg_admissions,
    double demand_multiplier)
    : config_(config),
      agg_admissions_(agg_admissions),
      logger_(MakeLogger("downgrade-alloc")),
      downgrade_selector_(config_.downgrade_selector()),
      downgrade_fv_source_(config_.downgrade_selector().downgrade_usage()
                               ? FVSource::kUsage
                               : FVSource::kPredictedDemand) {}

std::vector<proto::FlowAlloc> SimpleDowngradeAllocator::AllocAgg(
    absl::Time time, const proto::AggInfo& agg_info,
    proto::DebugAllocRecord::DebugState* debug_state) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  auto admissions_iter = agg_admissions_.find(agg_info.parent().flow());
  if (admissions_iter == agg_admissions_.end()) {
    SPDLOG_LOGGER_INFO(&logger_, "no admission for FG {}",
                       agg_info.parent().flow().ShortDebugString());
    return {};
  }

  const proto::FlowAlloc& admissions = admissions_iter->second;

  int64_t hipri_admission = admissions.hipri_rate_limit_bps();
  int64_t lopri_admission = admissions.lopri_rate_limit_bps();

  if (should_debug) {
    SPDLOG_LOGGER_INFO(&logger_, "allocating for time = {}", time);
    SPDLOG_LOGGER_INFO(&logger_, "hipri admission = {} lopri admission = {}",
                       hipri_admission, lopri_admission);
  }

  const int64_t lopri_bps = std::max<int64_t>(
      0, GetFlowVolume(agg_info.parent(), downgrade_fv_source_) - hipri_admission);

  double frac_lopri =
      static_cast<double>(lopri_bps) /
      static_cast<double>(GetFlowVolume(agg_info.parent(), downgrade_fv_source_));

  *debug_state->mutable_parent_alloc() = admissions;
  debug_state->set_frac_lopri_initial(frac_lopri);
  debug_state->set_frac_lopri_with_probing(frac_lopri);

  if (should_debug) {
    SPDLOG_LOGGER_INFO(&logger_, "lopri_frac = {}", frac_lopri);
  }

  frac_lopri = ClampFracLOPRI(&logger_, frac_lopri);

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
    debug_state->set_burstiness(0);
  }

  std::vector<bool> lopri_children;
  if (frac_lopri > 0) {
    lopri_children = downgrade_selector_.PickLOPRIChildren(agg_info, frac_lopri);
  } else {
    lopri_children = std::vector<bool>(agg_info.children_size(), false);
  }

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

  debug_state->set_frac_lopri_post_partition(frac_lopri_post_partition);
  debug_state->set_frac_lopri_final(std::min(frac_lopri, frac_lopri_post_partition));

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

  bool throttle_hipri = false;
  switch (config_.simple_downgrade_throttle_hipri()) {
    case proto::HTC_NEVER:
      // don't throttle
      break;
    case proto::HTC_WHEN_ABOVE_HIPRI_LIMIT:
      if (lopri_bps > 0) {
        throttle_hipri = true;
      }
      break;
    case proto::HTC_WHEN_ASSIGNED_LOPRI:
      if (lopri_demands.size() > 0) {
        throttle_hipri = true;
      }
      break;
    case proto::HTC_ALWAYS:
      throttle_hipri = true;
      break;
    default:
      SPDLOG_LOGGER_ERROR(&logger_, "unknown HipriThrottleCondition = {}",
                          config_.simple_downgrade_throttle_hipri());
  }

  int64_t hipri_limit = config_.oversub_factor() * (hipri_waterlevel + hipri_bonus);
  if (!throttle_hipri) {
    hipri_limit = kMaxChildBandwidthBps;
  }
  const int64_t lopri_limit = config_.oversub_factor() * (lopri_waterlevel + lopri_bonus);

  if (should_debug) {
    SPDLOG_LOGGER_INFO(&logger_, "throttle hipri = {} hipri limit = {} lopri limit = {}",
                       throttle_hipri, hipri_limit, lopri_limit);
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
