#ifndef HEYP_ALG_DOWNGRADE_IMPL_LARGEST_FIRST_H
#define HEYP_ALG_DOWNGRADE_IMPL_LARGEST_FIRST_H

#include "heyp/alg/downgrade/iface.h"

namespace heyp {

class LargestFirstDowngradeSelector : public DowngradeSelectorImpl {
 public:
  std::vector<bool> PickLOPRIChildren(const AggInfoView& agg_info,
                                      const double want_frac_lopri,
                                      spdlog::logger* logger) override;
};

}  // namespace heyp

#endif  // HEYP_ALG_DOWNGRADE_IMPL_LARGEST_FIRST_H
