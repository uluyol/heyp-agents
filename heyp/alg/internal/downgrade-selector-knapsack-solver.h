#ifndef HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_KNAPSACK_SOLVER_H_
#define HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_KNAPSACK_SOLVER_H_

#include "heyp/alg/internal/downgrade-selector-iface.h"

namespace heyp {
namespace internal {

class KnapsackSolverDowngradeSelector : public DowngradeSelectorImpl {
  std::vector<bool> PickLOPRIChildren(const AggInfoView& agg_info,
                                      const double want_frac_lopri,
                                      spdlog::logger* logger) override;
};

}  // namespace internal
}  // namespace heyp

#endif  // HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_KNAPSACK_SOLVER_H_
