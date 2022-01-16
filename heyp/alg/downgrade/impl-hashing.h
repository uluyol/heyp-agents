#ifndef HEYP_ALG_DOWNGRADE_IMPL_HASHING_H_
#define HEYP_ALG_DOWNGRADE_IMPL_HASHING_H_

#include <cstdbool>
#include <cstdint>

#include "heyp/alg/downgrade/hash-ring.h"
#include "heyp/alg/downgrade/iface.h"

namespace heyp {

class HashingDowngradeSelector : public DiffDowngradeSelectorImpl {
 public:
  DowngradeDiff PickChildren(const AggInfoView& agg_info, const double want_frac_lopri,
                             spdlog::logger* logger) override;

  bool IsLOPRI(uint64_t child_id) const {
    return lopri_.MatchingRanges().Contains(child_id);
  }

 private:
  HashRing lopri_;
};

}  // namespace heyp

#endif  // HEYP_ALG_DOWNGRADE_IMPL_HASHING_H_
