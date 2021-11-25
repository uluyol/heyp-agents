#include "heyp/alg/qos-downgrade.h"

#include <algorithm>
#include <memory>

#include "absl/strings/str_join.h"
#include "heyp/alg/agg-info-views.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/internal/downgrade-selector-hashing.h"
#include "heyp/alg/internal/downgrade-selector-heyp-sigcomm-20.h"
#include "heyp/alg/internal/downgrade-selector-knapsack-solver.h"
#include "heyp/alg/internal/downgrade-selector-largest-first.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

DowngradeSelector::DowngradeSelector(const proto::DowngradeSelector& selector)
    : logger_(MakeLogger("downgrade-selector")),
      downgrade_jobs_(selector.downgrade_jobs()) {
  std::vector<bool> selection;
  switch (selector.type()) {
    case proto::DS_HASHING:
      impl_ = std::make_unique<internal::HashingDowngradeSelector>();
      break;
    case proto::DS_HEYP_SIGCOMM20:
      impl_ = std::make_unique<internal::HeypSigcomm20DowngradeSelector>();
      break;
    case proto::DS_KNAPSACK_SOLVER:
      impl_ = std::make_unique<internal::KnapsackSolverDowngradeSelector>();
      break;
    case proto::DS_LARGEST_FIRST:
      impl_ = std::make_unique<internal::LargestFirstDowngradeSelector>();
      break;
    default:
      SPDLOG_LOGGER_CRITICAL(&logger_, "unsupported DowngradeSelectorType: {}",
                             selector.type());
      DumpStackTraceAndExit(5);
  }
}

std::vector<bool> DowngradeSelector::PickLOPRIChildren(const proto::AggInfo& agg_info,
                                                       const double want_frac_lopri) {
  TransparentView raw_view(agg_info);
  std::unique_ptr<JobLevelView> job_level_view;
  AggInfoView* view = &raw_view;

  if (downgrade_jobs_) {
    job_level_view = std::make_unique<JobLevelView>(agg_info);
    view = job_level_view.get();
  }
  std::vector<bool> selection =
      impl_->PickLOPRIChildren(*view, want_frac_lopri, &logger_);
  if (downgrade_jobs_) {
    std::vector<bool> host_selection(agg_info.children_size(), false);
    const std::vector<int>& job_of_host = job_level_view->job_index_of_host();
    H_SPDLOG_CHECK_EQ(&logger_, host_selection.size(), job_of_host.size());
    for (int i = 0; i < host_selection.size(); ++i) {
      H_SPDLOG_CHECK_GE(&logger_, job_of_host[i], 0);
      H_SPDLOG_CHECK_LT(&logger_, job_of_host[i], selection.size());
      host_selection[i] = selection[job_of_host[i]];
    }
    return host_selection;
  }
  return selection;
}

double FracAdmittedAtLOPRI(const proto::FlowInfo& parent,
                           const int64_t hipri_rate_limit_bps,
                           const int64_t lopri_rate_limit_bps) {
  bool maybe_admit = lopri_rate_limit_bps > 0;
  maybe_admit = maybe_admit && parent.predicted_demand_bps() > 0;
  maybe_admit = maybe_admit && parent.predicted_demand_bps() > hipri_rate_limit_bps;
  if (maybe_admit) {
    const double total_rate_limit_bps = hipri_rate_limit_bps + lopri_rate_limit_bps;
    const double total_admitted_demand_bps =
        std::min<double>(parent.predicted_demand_bps(), total_rate_limit_bps);
    return 1 - (hipri_rate_limit_bps / total_admitted_demand_bps);
  }
  return 0;
}

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

  if (agg_info.parent().predicted_demand_bps() < hipri_rate_limit_bps) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "predicted demand < hipri rate limit ({} < {})",
                         agg_info.parent().predicted_demand_bps(), hipri_rate_limit_bps);
    }
    return lopri_frac;
  }
  if (agg_info.parent().predicted_demand_bps() >
      demand_multiplier * hipri_rate_limit_bps) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(
          logger, "predicted demand > demand multipler * hipri rate limit ({} > {})",
          agg_info.parent().predicted_demand_bps(),
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
  int64_t smallest_child_demand_bps = agg_info.children(0).predicted_demand_bps();
  for (const proto::FlowInfo& child : agg_info.children()) {
    smallest_child_demand_bps =
        std::min(smallest_child_demand_bps, child.predicted_demand_bps());
  }

  if (smallest_child_demand_bps > lopri_rate_limit_bps) {
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "smallest child demand > lopri rate limit ({} > {})",
                         smallest_child_demand_bps, lopri_rate_limit_bps);
    }
    return lopri_frac;
  }

  double revised_frac = 1.00001 /* account for rounding error */ *
                        static_cast<double>(smallest_child_demand_bps) /
                        static_cast<double>(agg_info.parent().predicted_demand_bps());
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

}  // namespace heyp
