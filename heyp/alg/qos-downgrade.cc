#include "heyp/alg/qos-downgrade.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "absl/strings/str_join.h"
#include "flow-volume.h"
#include "heyp/alg/agg-info-views.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/downgrade/impl-hashing.h"
#include "heyp/alg/downgrade/impl-heyp-sigcomm-20.h"
#include "heyp/alg/downgrade/impl-knapsack-solver.h"
#include "heyp/alg/downgrade/impl-largest-first.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {
namespace {

std::unique_ptr<DowngradeSelectorImpl> GetSelector(
    const proto::DowngradeSelector& selector, spdlog::logger* logger) {
  switch (selector.type()) {
    case proto::DS_HASHING:
      return std::make_unique<HashingDowngradeSelector>();
      break;
    case proto::DS_HEYP_SIGCOMM20:
      return std::make_unique<HeypSigcomm20DowngradeSelector>();
      break;
    case proto::DS_KNAPSACK_SOLVER:
      return std::make_unique<KnapsackSolverDowngradeSelector>();
      break;
    case proto::DS_LARGEST_FIRST:
      return std::make_unique<LargestFirstDowngradeSelector>();
      break;
    default:
      SPDLOG_LOGGER_CRITICAL(logger, "unsupported DowngradeSelectorType: {}",
                             selector.type());
      DumpStackTraceAndExit(5);
  }
  return nullptr;
}

}  // namespace

DowngradeSelector::DowngradeSelector(const proto::DowngradeSelector& selector)
    : logger_(MakeLogger("downgrade-selector")),
      impl_(GetSelector(selector, &logger_)),
      downgrade_jobs_(selector.downgrade_jobs()),
      downgrade_usage_(selector.downgrade_usage()) {}

std::vector<bool> DowngradeSelector::PickLOPRIChildren(const proto::AggInfo& agg_info,
                                                       const double want_frac_lopri) {
  if (downgrade_jobs_) {
    std::optional<JobLevelView> job_level_view;
    if (downgrade_usage_) {
      job_level_view.emplace(JobLevelView::Create<FVSource::kUsage>(agg_info));
    } else {
      job_level_view.emplace(JobLevelView::Create<FVSource::kPredictedDemand>(agg_info));
    }
    std::vector<bool> job_selection =
        impl_->PickLOPRIChildren(*job_level_view, want_frac_lopri, &logger_);

    std::vector<bool> host_selection(agg_info.children_size(), false);
    const std::vector<int>& job_of_host = job_level_view->job_index_of_host();
    H_SPDLOG_CHECK_EQ(&logger_, host_selection.size(), job_of_host.size());
    for (int i = 0; i < host_selection.size(); ++i) {
      H_SPDLOG_CHECK_GE(&logger_, job_of_host[i], 0);
      H_SPDLOG_CHECK_LT(&logger_, job_of_host[i], job_selection.size());
      host_selection[i] = job_selection[job_of_host[i]];
    }
    return host_selection;
  }

  std::optional<HostLevelView> view;
  if (downgrade_usage_) {
    view.emplace(HostLevelView::Create<FVSource::kUsage>(agg_info));
  } else {
    view.emplace(HostLevelView::Create<FVSource::kPredictedDemand>(agg_info));
  }
  return impl_->PickLOPRIChildren(*view, want_frac_lopri, &logger_);
}

DowngradeFracController::DowngradeFracController(
    const proto::DowngradeFracController& config)
    : config_(config), logger_(MakeLogger("downgrade-frac-controller")) {}

constexpr static bool kDebugDowngradeFracController = true;

// Translated from ../../go/intradc/feedbacksim/control.go
double DowngradeFracController::TrafficFracToDowngradeRaw(double cur, double setpoint,
                                                          double input2output_conversion,
                                                          double max_task_usage_frac) {
  const double err = cur - setpoint;
  if (0 < err && err < config_.ignore_overage_below()) {
    if (kDebugDowngradeFracController) {
      SPDLOG_LOGGER_INFO(&logger_, "overage in noise [err = {} cur = {} setpoint = {}]",
                         err, cur, setpoint);
    }
    return 0;
  }
  if (0 < err &&
      err < config_.ignore_overage_by_coarseness_multiplier() * max_task_usage_frac) {
    if (kDebugDowngradeFracController) {
      SPDLOG_LOGGER_INFO(&logger_,
                         "too coarse to response to overage [err = {} cur = {} setpoint "
                         "= {} max_task_usage_frac = {}]",
                         err, cur, setpoint, max_task_usage_frac);
    }
    return 0;
  }
  double x = config_.prop_gain() * err;
  x *= input2output_conversion;
  if (x < -1 || x > 1) {
    SPDLOG_LOGGER_CRITICAL(&logger_,
                           "saw invalid pre-clamping downgrade frac inc = {} "
                           "cur = {} setpoint = {} i2o = {} max task = {} "
                           "controller = {}",
                           x, cur, setpoint, input2output_conversion, max_task_usage_frac,
                           config_.ShortDebugString());
    DumpStackTraceAndExit(1);
  }
  double xorig = x;
  x = copysign(fmin(fabs(x), config_.max_inc()), x);
  if (kDebugDowngradeFracController) {
    SPDLOG_LOGGER_INFO(
        &logger_,
        "inc = {} inc pre-clamping = {} [cur = {} setpoint = {} i2o = {} max task = {}]",
        x, xorig, cur, setpoint, input2output_conversion, max_task_usage_frac);
  }
  return x;
}

double DowngradeFracController::TrafficFracToDowngrade(double hipri_usage_bps,
                                                       double lopri_usage_bps,
                                                       double hipri_rate_limit_bps,
                                                       double max_task_usage_bps) {
  if (kDebugDowngradeFracController) {
    SPDLOG_LOGGER_INFO(
        &logger_,
        "hipri_usage: {} lopri_usage: {} hipri_rate_limit: {} max_task_usage: {}",
        hipri_usage_bps, lopri_usage_bps, hipri_rate_limit_bps, max_task_usage_bps);
  }
  double total_usage_bps = hipri_usage_bps + lopri_usage_bps;
  double cur = hipri_usage_bps / total_usage_bps;
  double setpoint = hipri_rate_limit_bps / total_usage_bps;
  return TrafficFracToDowngradeRaw(cur, setpoint, 1.0,
                                   max_task_usage_bps / total_usage_bps);
}

template <FVSource vol_source>
double FracAdmittedAtLOPRI(const proto::FlowInfo& parent,
                           const int64_t hipri_rate_limit_bps,
                           const int64_t lopri_rate_limit_bps) {
  bool maybe_admit = lopri_rate_limit_bps > 0;
  maybe_admit = maybe_admit && GetFlowVolume(parent, vol_source) > 0;
  maybe_admit = maybe_admit && GetFlowVolume(parent, vol_source) > hipri_rate_limit_bps;
  if (maybe_admit) {
    const double total_rate_limit_bps = hipri_rate_limit_bps + lopri_rate_limit_bps;
    const double total_admitted_demand_bps =
        std::min<double>(GetFlowVolume(parent, vol_source), total_rate_limit_bps);
    return 1 - (hipri_rate_limit_bps / total_admitted_demand_bps);
  }
  return 0;
}

template double FracAdmittedAtLOPRI<FVSource::kPredictedDemand>(
    const proto::FlowInfo& parent, const int64_t hipri_rate_limit_bps,
    const int64_t lopri_rate_limit_bps);

template double FracAdmittedAtLOPRI<FVSource::kUsage>(const proto::FlowInfo& parent,
                                                      const int64_t hipri_rate_limit_bps,
                                                      const int64_t lopri_rate_limit_bps);

template <FVSource vol_source>
double FracAdmittedAtLOPRIToProbe(const proto::AggInfo& agg_info,
                                  const int64_t hipri_rate_limit_bps,
                                  const int64_t lopri_rate_limit_bps,
                                  const double demand_multiplier, const double lopri_frac,
                                  spdlog::logger* logger) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  if (should_debug) {
    SPDLOG_LOGGER_INFO(logger, "agg_info: {}", agg_info.DebugString());
    SPDLOG_LOGGER_INFO(logger, "cur limits: ({}, {})", hipri_rate_limit_bps,
                       lopri_rate_limit_bps);
    SPDLOG_LOGGER_INFO(logger, "demand_multiplier: {}", demand_multiplier);
    SPDLOG_LOGGER_INFO(logger, "initial lopri_frac: {}", lopri_frac);
  }

  if (GetFlowVolume(agg_info.parent(), vol_source) < hipri_rate_limit_bps) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "flow volume < hipri rate limit ({} < {})",
                         GetFlowVolume(agg_info.parent(), vol_source),
                         hipri_rate_limit_bps);
    }
    return lopri_frac;
  }
  if (GetFlowVolume(agg_info.parent(), vol_source) >
      demand_multiplier * hipri_rate_limit_bps) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger,
                         "flow volume > volume multipler * hipri rate limit ({} > {})",
                         GetFlowVolume(agg_info.parent(), vol_source),
                         demand_multiplier * hipri_rate_limit_bps);
    }
    return lopri_frac;
  }
  if (agg_info.children_size() == 0) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "no children");
    }
    return lopri_frac;
  }
  int64_t smallest_child_demand_bps = GetFlowVolume(agg_info.children(0), vol_source);
  for (const proto::FlowInfo& child : agg_info.children()) {
    smallest_child_demand_bps =
        std::min(smallest_child_demand_bps, GetFlowVolume(child, vol_source));
  }

  if (smallest_child_demand_bps > lopri_rate_limit_bps) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "smallest child volume > lopri rate limit ({} > {})",
                         smallest_child_demand_bps, lopri_rate_limit_bps);
    }
    return lopri_frac;
  }

  double revised_frac = 1.00001 /* account for rounding error */ *
                        static_cast<double>(smallest_child_demand_bps) /
                        static_cast<double>(GetFlowVolume(agg_info.parent(), vol_source));
  if (revised_frac > lopri_frac) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "revised lopri frac from {} to {}", lopri_frac,
                         revised_frac);
    }
    return revised_frac;
  } else if (should_debug) {
    SPDLOG_LOGGER_INFO(logger,
                       "existing lopri frac ({}) is larger than needed for probing ({})",
                       lopri_frac, revised_frac);
  }
  return lopri_frac;
}

template double FracAdmittedAtLOPRIToProbe<FVSource::kPredictedDemand>(
    const proto::AggInfo& agg_info, const int64_t hipri_rate_limit_bps,
    const int64_t lopri_rate_limit_bps, const double demand_multiplier,
    const double lopri_frac, spdlog::logger* logger);

template <>
double FracAdmittedAtLOPRIToProbe<FVSource::kUsage>(const proto::AggInfo& agg_info,
                                                    const int64_t hipri_rate_limit_bps,
                                                    const int64_t lopri_rate_limit_bps,
                                                    const double demand_multiplier,
                                                    const double lopri_frac,
                                                    spdlog::logger* logger) {
  SPDLOG_LOGGER_CRITICAL(logger, "{}: not implemented for usage-based flow volumes",
                         __func__);
  DumpStackTraceAndExit(33);
  return 666;
}

}  // namespace heyp
