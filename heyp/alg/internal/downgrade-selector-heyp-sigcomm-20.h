#ifndef HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_HEYP_SIGCOMM_20_H_
#define HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_HEYP_SIGCOMM_20_H_

#include "heyp/alg/internal/downgrade-selector-iface.h"

namespace heyp {
namespace internal {

class HeypSigcomm20DowngradeSelector : public DowngradeSelectorImpl {
  std::vector<bool> PickLOPRIChildren(const AggInfoView& agg_info,
                                      const double want_frac_lopri,
                                      spdlog::logger* logger) override;
};

}  // namespace internal
}  // namespace heyp

#endif  // HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_HEYP_SIGCOMM_20_H_
