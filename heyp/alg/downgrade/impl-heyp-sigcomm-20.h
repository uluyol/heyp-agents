#ifndef HEYP_ALG_DOWNGRADE_IMPL_HEYP_SIGCOMM_20_H_
#define HEYP_ALG_DOWNGRADE_IMPL_HEYP_SIGCOMM_20_H_

#include "heyp/alg/downgrade/iface.h"

namespace heyp {

class HeypSigcomm20DowngradeSelector : public DowngradeSelectorImpl {
  std::vector<bool> PickLOPRIChildren(const AggInfoView& agg_info,
                                      const double want_frac_lopri,
                                      spdlog::logger* logger) override;
};

}  // namespace heyp

#endif  // HEYP_ALG_DOWNGRADE_IMPL_HEYP_SIGCOMM_20_H_
