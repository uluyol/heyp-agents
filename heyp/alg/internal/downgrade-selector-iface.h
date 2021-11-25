#ifndef HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_IFACE_H_
#define HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_IFACE_H_

#include <cstdbool>
#include <vector>

#include "heyp/alg/agg-info-views.h"
#include "heyp/log/spdlog.h"

namespace heyp {
namespace internal {

class DowngradeSelectorImpl {
 public:
  virtual ~DowngradeSelectorImpl() {}
  virtual std::vector<bool> PickLOPRIChildren(const AggInfoView& agg_info,
                                              const double want_frac_lopri,
                                              spdlog::logger* logger) = 0;
};

}  // namespace internal
}  // namespace heyp

#endif  // HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_IFACE_H_
