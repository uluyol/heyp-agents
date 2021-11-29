#ifndef HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_LARGEST_FIRST_H
#define HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_LARGEST_FIRST_H

#include "heyp/alg/internal/downgrade-selector-iface.h"

namespace heyp {
namespace internal {

class LargestFirstDowngradeSelector : public DowngradeSelectorImpl {
 public:
  std::vector<bool> PickLOPRIChildren(const AggInfoView& agg_info,
                                      const double want_frac_lopri,
                                      spdlog::logger* logger) override;
};

}  // namespace internal
}  // namespace heyp

#endif  // HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_LARGEST_FIRST_H
