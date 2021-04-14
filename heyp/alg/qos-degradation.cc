#include "heyp/alg/qos-degradation.h"

#include <algorithm>

#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"

namespace heyp {

template <bool StateToIncrease>
void GreedyAssignToMinimizeGap(GreedyAssignToMinimizeGapArgs args,
                               std::vector<bool>& lopri_children) {
  for (size_t child_i : args.children_sorted_by_dec_demand) {
    if (lopri_children[child_i] == StateToIncrease) {
      continue;  // child already belongs to our bin, don't flip
    }
    // Try to flip child_i to our bin.
    int64_t next_demand =
        args.cur_demand + args.agg_info.children(child_i).predicted_demand_bps();
    if (next_demand > args.want_demand) {
      continue;  // flipping child_i overshoots the goal
    }
    // Safe to flip child_i;
    lopri_children[child_i] = StateToIncrease;
    args.cur_demand = next_demand;
  }
}

// Instantiate both (true/false) variants
template void GreedyAssignToMinimizeGap<false>(GreedyAssignToMinimizeGapArgs args,
                                               std::vector<bool>& lopri_children);
template void GreedyAssignToMinimizeGap<true>(GreedyAssignToMinimizeGapArgs args,
                                              std::vector<bool>& lopri_children);

struct BitmapFormatter {
  void operator()(std::string* out, bool b) {
    if (b) {
      out->push_back('1');
    } else {
      out->push_back('0');
    }
  }
};

std::vector<bool> HeypSigcomm20PickLOPRIChildren(const proto::AggInfo& agg_info,
                                                 const double want_frac_lopri) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  if (should_debug) {
    LOG(INFO) << "agg_info: " << agg_info.DebugString();
    LOG(INFO) << "want_frac_lopri: " << want_frac_lopri;
  }

  std::vector<bool> lopri_children(agg_info.children_size(), false);
  int64_t total_demand = 0;
  int64_t lopri_demand = 0;
  std::vector<size_t> children_sorted_by_dec_demand(agg_info.children_size(), 0);
  for (size_t i = 0; i < agg_info.children_size(); ++i) {
    children_sorted_by_dec_demand[i] = i;
    const auto& c = agg_info.children(i);
    total_demand += c.predicted_demand_bps();
    if (c.currently_lopri()) {
      lopri_children[i] = true;
      lopri_demand += c.predicted_demand_bps();
    }
  }

  if (total_demand == 0) {
    if (should_debug) {
      LOG(INFO) << "no demand";
    }
    // Don't use LOPRI if all demand is zero.
    return std::vector<bool>(agg_info.children_size(), false);
  }

  std::sort(children_sorted_by_dec_demand.begin(), children_sorted_by_dec_demand.end(),
            [&agg_info](size_t lhs, size_t rhs) -> bool {
              int64_t lhs_demand = agg_info.children(lhs).predicted_demand_bps();
              int64_t rhs_demand = agg_info.children(rhs).predicted_demand_bps();
              if (lhs_demand == rhs_demand) {
                return lhs > rhs;
              }
              return lhs_demand > rhs_demand;
            });

  if (static_cast<double>(lopri_demand) / static_cast<double>(total_demand) >
      want_frac_lopri) {
    // Move from LOPRI to HIPRI
    int64_t hipri_demand = total_demand - lopri_demand;
    int64_t want_demand = (1 - want_frac_lopri) * total_demand;
    GreedyAssignToMinimizeGap<false>(
        {
            .cur_demand = hipri_demand,
            .want_demand = want_demand,
            .children_sorted_by_dec_demand = children_sorted_by_dec_demand,
            .agg_info = agg_info,
        },
        lopri_children);
  } else {
    // Move from HIPRI to LOPRI
    int64_t want_demand = want_frac_lopri * total_demand;
    GreedyAssignToMinimizeGap<true>(
        {
            .cur_demand = lopri_demand,
            .want_demand = want_demand,
            .children_sorted_by_dec_demand = children_sorted_by_dec_demand,
            .agg_info = agg_info,
        },
        lopri_children);
  }

  if (should_debug) {
    LOG(INFO) << "picked LOPRI assignment: "
              << absl::StrJoin(lopri_children, "", BitmapFormatter());
  }

  return lopri_children;
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

bool ShouldProbeLOPRI(const proto::AggInfo& agg_info, const int64_t hipri_rate_limit_bps,
                      const int64_t lopri_rate_limit_bps, double demand_multiplier,
                      double* lopri_frac) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  if (should_debug) {
    LOG(INFO) << "agg_info: " << agg_info.DebugString();
    LOG(INFO) << "cur limits: (" << hipri_rate_limit_bps << ", " << lopri_rate_limit_bps
              << ")";
    LOG(INFO) << "demand_multiplier: " << demand_multiplier;
    LOG(INFO) << "(initial) lopri_frac: " << *lopri_frac;
  }

  if (agg_info.parent().predicted_demand_bps() < hipri_rate_limit_bps) {
    if (should_debug) {
      LOG(INFO) << "predicted demand < hipri rate limit ("
                << agg_info.parent().predicted_demand_bps() << " < "
                << hipri_rate_limit_bps << ")";
    }
    return false;
  }
  if (agg_info.parent().predicted_demand_bps() >
      demand_multiplier * hipri_rate_limit_bps) {
    if (should_debug) {
      LOG(INFO) << "predicted demand > demand multipler * hipri rate limit ("
                << agg_info.parent().predicted_demand_bps() << " > "
                << demand_multiplier * hipri_rate_limit_bps << ")";
    }
    return false;
  }
  if (agg_info.children_size() == 0) {
    if (should_debug) {
      LOG(INFO) << "no children";
    }
    return false;
  }
  int64_t smallest_child_demand_bps = agg_info.children(0).predicted_demand_bps();
  for (const proto::FlowInfo& child : agg_info.children()) {
    smallest_child_demand_bps =
        std::min(smallest_child_demand_bps, child.predicted_demand_bps());
  }

  if (smallest_child_demand_bps > lopri_rate_limit_bps) {
    if (should_debug) {
      LOG(INFO) << "smallest child demand > lopri rate limit ("
                << smallest_child_demand_bps << " > " << lopri_rate_limit_bps << ")";
    }
    return false;
  }

  double revised_frac = 1.00001 /* account for rounding error */ *
                        static_cast<double>(smallest_child_demand_bps) /
                        static_cast<double>(agg_info.parent().predicted_demand_bps());
  if (revised_frac > *lopri_frac) {
    if (should_debug) {
      LOG(INFO) << "revised lopri frac from " << *lopri_frac << " to " << revised_frac;
    }
    *lopri_frac = revised_frac;
  } else if (should_debug) {
    LOG(INFO) << "existing lopri frac (" << *lopri_frac
              << ") is larger than needed for probing (" << revised_frac << ")";
  }

  return true;
}

}  // namespace heyp
