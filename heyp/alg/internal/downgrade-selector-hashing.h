#ifndef HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_HASHING_H_
#define HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_HASHING_H_

#include "heyp/alg/internal/downgrade-selector-iface.h"
#include "heyp/alg/internal/hash-ring.h"

namespace heyp {
namespace internal {

class HashingDowngradeSelector : public DowngradeSelectorImpl {
 public:
  std::vector<bool> PickLOPRIChildren(const AggInfoView& agg_info,
                                      const double want_frac_lopri,
                                      spdlog::logger* logger) override;

 private:
  HashRing lopri_;
};

}  // namespace internal
}  // namespace heyp

#endif  // HEYP_ALG_INTERNAL_DOWNGRADE_SELECTOR_HASHING_H_
