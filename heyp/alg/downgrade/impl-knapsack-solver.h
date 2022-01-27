#ifndef HEYP_ALG_DOWNGRADE_IMPL_KNAPSACK_SOLVER_H_
#define HEYP_ALG_DOWNGRADE_IMPL_KNAPSACK_SOLVER_H_

#include "heyp/alg/downgrade/iface.h"

namespace heyp {

class KnapsackSolverDowngradeSelector : public DowngradeSelectorImpl {
 public:
  KnapsackSolverDowngradeSelector(double time_limit_sec = -1)
      : time_limit_sec_(time_limit_sec) {}

  std::vector<bool> PickLOPRIChildren(const AggInfoView& agg_info,
                                      const double want_frac_lopri,
                                      spdlog::logger* logger) override;

 private:
  const double time_limit_sec_;
};

}  // namespace heyp

#endif  // HEYP_ALG_DOWNGRADE_IMPL_KNAPSACK_SOLVER_H_
