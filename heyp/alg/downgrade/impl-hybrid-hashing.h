#ifndef HEYP_ALG_DOWNGRADE_IMPL_HYBRID_HASHING_H_
#define HEYP_ALG_DOWNGRADE_IMPL_HYBRID_HASHING_H_

#include <cstdint>

#include "heyp/alg/downgrade/hash-ring.h"
#include "heyp/alg/downgrade/iface.h"
#include "heyp/alg/flow-volume.h"
#include "heyp/log/spdlog.h"

namespace heyp {

template <FVSource vol_source>
class HybridHashingDowngradeSelector : public DowngradeSelectorImpl {
 public:
  explicit HybridHashingDowngradeSelector(int64_t num_demand_aware)
      : num_demand_aware_(num_demand_aware) {
    H_ASSERT_GE(num_demand_aware, 0);
  }

  std::vector<bool> PickLOPRIChildren(const AggInfoView& agg_info,
                                      const double want_frac_lopri,
                                      spdlog::logger* logger) override;

 private:
  const int64_t num_demand_aware_;
  std::vector<std::pair<uint64_t, uint64_t>> flow_volumes_and_id_;
  HashRing lopri_;
  absl::flat_hash_map<uint64_t> pinned_hosts_;
};

}  // namespace heyp

#endif  // HEYP_ALG_DOWNGRADE_IMPL_HYBRID_HASHING_H_
