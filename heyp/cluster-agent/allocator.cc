#include "heyp/cluster-agent/allocator.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/qos-downgrade.h"
#include "heyp/alg/rate-limits.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/alg.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/proto/monitoring.pb.h"
#include "routing-algos/alg/max-min-fairness.h"

namespace heyp {

class PerAggAllocator {
 public:
  virtual ~PerAggAllocator() = default;
  virtual std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info,
      proto::DebugAllocRecord::DebugState* debug_state) = 0;
};

constexpr int kNumAllocCores = 8;

ClusterAllocator::ClusterAllocator(std::unique_ptr<PerAggAllocator> alloc,
                                   NdjsonLogger* alloc_recorder)
    : alloc_(std::move(alloc)),
      logger_(MakeLogger("cluster-alloc")),
      exec_(kNumAllocCores),
      alloc_recorder_(alloc_recorder) {}

ClusterAllocator::~ClusterAllocator() {}

void ClusterAllocator::Reset() {
  absl::MutexLock l(&mu_);
  group_ = exec_.NewTaskGroup();
  allocs_.partial_sets.clear();
}

void ClusterAllocator::AddInfo(absl::Time time, const proto::AggInfo& info) {
  group_->AddTaskNoStatus([time, info, this] {
    proto::DebugAllocRecord record;
    std::vector<proto::FlowAlloc> a =
        this->alloc_->AllocAgg(time, info, record.mutable_debug_state());
    absl::MutexLock l(&this->mu_);
    if (this->alloc_recorder_ != nullptr) {
      record.set_timestamp(absl::FormatTime(time, absl::UTCTimeZone()));
      *record.mutable_info() = info;
      *record.mutable_flow_allocs() = {a.begin(), a.end()};
      absl::Status log_status = this->alloc_recorder_->Write(record);
      if (!log_status.ok()) {
        SPDLOG_LOGGER_WARN(&logger_, "failed to log allocation record: {}", log_status);
      }
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
      : config_(config),
        agg_admissions_(std::move(agg_admissions)),
        logger_(MakeLogger("bwe-alloc")) {}

  std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info,
      proto::DebugAllocRecord::DebugState* debug_state) override {
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

    routing_algos::SingleLinkMaxMinFairnessProblem problem;
    int64_t waterlevel = problem.ComputeWaterlevel(cluster_admission, {demands});

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

 private:
  const proto::ClusterAllocatorConfig config_;
  const FlowMap<proto::FlowAlloc> agg_admissions_;
  spdlog::logger logger_;
};

class HeypSigcomm20Allocator : public PerAggAllocator {
 private:
  struct PerAggState {
    proto::FlowAlloc alloc;
    double frac_lopri = 0;
    double frac_lopri_with_probing = 0;
    absl::Time last_time = absl::UnixEpoch();
    int64_t last_cum_hipri_usage_bytes = 0;
    int64_t last_cum_lopri_usage_bytes = 0;
  };

  const proto::ClusterAllocatorConfig config_;
  const double demand_multiplier_;
  spdlog::logger logger_;
  FlowMap<PerAggState> agg_states_;

 public:
  HeypSigcomm20Allocator(const proto::ClusterAllocatorConfig& config,
                         FlowMap<proto::FlowAlloc> agg_admissions,
                         double demand_multiplier)
      : config_(config),
        demand_multiplier_(demand_multiplier),
        logger_(MakeLogger("heyp-sigcomm20-alloc")) {
    for (const auto& flow_alloc_pair : agg_admissions) {
      agg_states_[flow_alloc_pair.first] = {.alloc = flow_alloc_pair.second};
    }
  }

  std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info,
      proto::DebugAllocRecord::DebugState* debug_state) override {
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

    cur_state.frac_lopri =
        FracAdmittedAtLOPRI(agg_info.parent(), hipri_admission, lopri_admission);
    if (config_.heyp_probe_lopri_when_ambiguous()) {
      cur_state.frac_lopri_with_probing =
          FracAdmittedAtLOPRIToProbe(agg_info, hipri_admission, lopri_admission,
                                     demand_multiplier_, cur_state.frac_lopri, &logger_);
    } else {
      cur_state.frac_lopri_with_probing = cur_state.frac_lopri;
    }

    debug_state->set_frac_lopri_initial(cur_state.frac_lopri);
    debug_state->set_frac_lopri_with_probing(cur_state.frac_lopri_with_probing);

    if (should_debug) {
      SPDLOG_LOGGER_INFO(&logger_, "lopri_frac = {} lopri_frac_with_debugging = {}",
                         cur_state.frac_lopri, cur_state.frac_lopri_with_probing);
    }

    ABSL_ASSERT(cur_state.frac_lopri_with_probing >= 0);
    ABSL_ASSERT(cur_state.frac_lopri_with_probing <= 1);

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
        PickLOPRIChildren(agg_info, cur_state.frac_lopri_with_probing,
                          config_.downgrade_selector(), &logger_);

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

    routing_algos::SingleLinkMaxMinFairnessProblem problem;
    int64_t hipri_waterlevel =
        problem.ComputeWaterlevel(hipri_admission, {hipri_demands});
    int64_t lopri_waterlevel =
        problem.ComputeWaterlevel(lopri_admission, {lopri_demands});

    if (should_debug) {
      SPDLOG_LOGGER_INFO(&logger_, "hipri waterlevel = {} lopri waterlevel = {}",
                         hipri_waterlevel, lopri_waterlevel);
    }
    int64_t hipri_bonus = 0;
    int64_t lopri_bonus = 0;
    if (config_.enable_bonus()) {
      hipri_bonus =
          EvenlyDistributeExtra(hipri_admission, hipri_demands, hipri_waterlevel);
      lopri_bonus =
          EvenlyDistributeExtra(lopri_admission, lopri_demands, lopri_waterlevel);
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

    const int64_t hipri_limit =
        config_.oversub_factor() * (hipri_waterlevel + hipri_bonus);
    const int64_t lopri_limit =
        config_.oversub_factor() * (lopri_waterlevel + lopri_bonus);

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
};

class DowngradeAllocator : public PerAggAllocator {
 private:
  const proto::ClusterAllocatorConfig config_;
  const FlowMap<proto::FlowAlloc> agg_admissions_;
  spdlog::logger logger_;

 public:
  DowngradeAllocator(const proto::ClusterAllocatorConfig& config,
                     FlowMap<proto::FlowAlloc> agg_admissions, double demand_multiplier)
      : config_(config),
        agg_admissions_(agg_admissions),
        logger_(MakeLogger("downgrade-alloc")) {}

  std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info,
      proto::DebugAllocRecord::DebugState* debug_state) override {
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

    const int64_t lopri_bps =
        std::max<int64_t>(0, agg_info.parent().predicted_demand_bps() - hipri_admission);

    const double frac_lopri =
        static_cast<double>(lopri_bps) /
        static_cast<double>(agg_info.parent().predicted_demand_bps());

    *debug_state->mutable_parent_alloc() = admissions;
    debug_state->set_frac_lopri_initial(frac_lopri);
    debug_state->set_frac_lopri_with_probing(frac_lopri);

    if (should_debug) {
      SPDLOG_LOGGER_INFO(&logger_, "lopri_frac = {}", frac_lopri);
    }

    ABSL_ASSERT(frac_lopri >= 0);
    ABSL_ASSERT(frac_lopri <= 1);

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
      lopri_children =
          PickLOPRIChildren(agg_info, frac_lopri, config_.downgrade_selector(), &logger_);
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

    routing_algos::SingleLinkMaxMinFairnessProblem problem;
    int64_t hipri_waterlevel =
        problem.ComputeWaterlevel(hipri_admission, {hipri_demands});
    int64_t lopri_waterlevel =
        problem.ComputeWaterlevel(lopri_admission, {lopri_demands});

    if (should_debug) {
      SPDLOG_LOGGER_INFO(&logger_, "hipri waterlevel = {} lopri waterlevel = {}",
                         hipri_waterlevel, lopri_waterlevel);
    }
    int64_t hipri_bonus = 0;
    int64_t lopri_bonus = 0;
    if (config_.enable_bonus()) {
      hipri_bonus =
          EvenlyDistributeExtra(hipri_admission, hipri_demands, hipri_waterlevel);
      lopri_bonus =
          EvenlyDistributeExtra(lopri_admission, lopri_demands, lopri_waterlevel);
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

    const int64_t hipri_limit =
        config_.oversub_factor() * (hipri_waterlevel + hipri_bonus);
    const int64_t lopri_limit =
        config_.oversub_factor() * (lopri_waterlevel + lopri_bonus);

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
};

class NopAllocator : public PerAggAllocator {
 private:
  spdlog::logger logger_;

 public:
  NopAllocator() : logger_(MakeLogger("nop-alloc")) {}

  std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info,
      proto::DebugAllocRecord::DebugState* debug_state) {
    const bool should_debug = DebugQosAndRateLimitSelection();
    if (should_debug) {
      if (should_debug) {
        SPDLOG_LOGGER_INFO(&logger_, "returning empty alloc for time = {}", time);
      }
    }
    return {};
  }
};

class HostPatternAllocator : public PerAggAllocator {
 private:
  spdlog::logger logger_;
  FlowMap<std::vector<proto::FlowAlloc>> per_host_alloc_patterns_;
  size_t next_;

 public:
  HostPatternAllocator(const proto::ClusterAllocatorConfig& config)
      : logger_(MakeLogger("host-pattern-alloc")), next_(0) {
    for (const proto::FixedClusterHostAllocs& per_host_alloc_pattern :
         config.fixed_host_alloc_patterns()) {
      per_host_alloc_patterns_[per_host_alloc_pattern.cluster()] = {
          per_host_alloc_pattern.per_host_alloc_pattern().begin(),
          per_host_alloc_pattern.per_host_alloc_pattern().end()};
    }
  }

  std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info,
      proto::DebugAllocRecord::DebugState* debug_state) {
    const bool should_debug = DebugQosAndRateLimitSelection();

    auto alloc_pattern_iter = per_host_alloc_patterns_.find(agg_info.parent().flow());
    if (alloc_pattern_iter == per_host_alloc_patterns_.end()) {
      SPDLOG_LOGGER_INFO(&logger_, "no admission for FG {}",
                         agg_info.parent().flow().ShortDebugString());
      return {};
    }

    const std::vector<proto::FlowAlloc>& alloc_pattern = alloc_pattern_iter->second;

    if (alloc_pattern.empty()) {
      return {};
    }

    if (should_debug) {
      if (should_debug) {
        SPDLOG_LOGGER_INFO(&logger_, "allocating for time = {} step = {}", time, next_);
      }
    }

    const proto::FlowAlloc& all_hosts_alloc = alloc_pattern[next_ % alloc_pattern.size()];
    next_++;

    std::vector<proto::FlowAlloc> allocs;
    allocs.reserve(agg_info.children_size());
    for (size_t i = 0; i < agg_info.children_size(); ++i) {
      proto::FlowAlloc alloc;
      *alloc.mutable_flow() = agg_info.children(i).flow();
      alloc.set_hipri_rate_limit_bps(all_hosts_alloc.hipri_rate_limit_bps());
      alloc.set_lopri_rate_limit_bps(all_hosts_alloc.lopri_rate_limit_bps());
      allocs.push_back(std::move(alloc));
    }
    return allocs;
  }
};

}  // namespace

absl::StatusOr<std::unique_ptr<ClusterAllocator>> ClusterAllocator::Create(
    const proto::ClusterAllocatorConfig& config,
    const proto::AllocBundle& cluster_wide_allocs, double demand_multiplier,
    NdjsonLogger* alloc_recorder) {
  FlowMap<proto::FlowAlloc> cluster_admissions = ToAdmissionsMap(cluster_wide_allocs);
  switch (config.type()) {
    case proto::CA_NOP:
      return absl::WrapUnique(
          new ClusterAllocator(absl::make_unique<NopAllocator>(), alloc_recorder));
    case proto::CA_BWE:
      return absl::WrapUnique(new ClusterAllocator(
          absl::make_unique<BweAggAllocator>(config, std::move(cluster_admissions)),
          alloc_recorder));
    case proto::CA_HEYP_SIGCOMM20:
      return absl::WrapUnique(new ClusterAllocator(
          absl::make_unique<HeypSigcomm20Allocator>(config, std::move(cluster_admissions),
                                                    demand_multiplier),
          alloc_recorder));
    case proto::CA_SIMPLE_DOWNGRADE:
      return absl::WrapUnique(new ClusterAllocator(
          absl::make_unique<DowngradeAllocator>(config, std::move(cluster_admissions),
                                                demand_multiplier),
          alloc_recorder));
    case proto::CA_FIXED_HOST_PATTERN:
      return absl::WrapUnique(new ClusterAllocator(
          absl::make_unique<HostPatternAllocator>(config), alloc_recorder));
  }
  std::cerr << "unreachable: got cluster allocator type: " << config.type() << "\n";
  DumpStackTraceAndExit(5);
  return nullptr;
}

}  // namespace heyp
