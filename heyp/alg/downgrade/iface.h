#ifndef HEYP_ALG_DOWNGRADE_IFACE_H_
#define HEYP_ALG_DOWNGRADE_IFACE_H_

#include <cstdbool>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "heyp/alg/agg-info-views.h"
#include "heyp/alg/unordered-ids.h"
#include "heyp/log/spdlog.h"

namespace heyp {

class DowngradeSelectorImpl {
 public:
  virtual ~DowngradeSelectorImpl() {}
  virtual std::vector<bool> PickLOPRIChildren(const AggInfoView& agg_info,
                                              const double want_frac_lopri,
                                              spdlog::logger* logger) = 0;
};

struct DowngradeDiff {
  UnorderedIds to_downgrade;
  UnorderedIds to_upgrade;
};

class DiffDowngradeSelectorImpl : public DowngradeSelectorImpl {
 public:
  virtual ~DiffDowngradeSelectorImpl() {}

  virtual DowngradeDiff PickChildren(const AggInfoView& agg_info,
                                     const double want_frac_lopri,
                                     spdlog::logger* logger) = 0;

  std::vector<bool> PickLOPRIChildren(const AggInfoView& agg_info,
                                      const double want_frac_lopri,
                                      spdlog::logger* logger) override;

 private:
  // last_is_lopri_ is a map of previous child id -> lopri/hipri assignments.
  // We need the map so that QoS decsions stick even when they are not applied
  // immediately.
  absl::flat_hash_map<uint64_t, bool> last_is_lopri_;
};

std::string ToString(const DowngradeDiff& diff, std::string_view indent = "");

bool operator==(const DowngradeDiff& lhs, const DowngradeDiff& rhs);
std::ostream& operator<<(std::ostream& os, const DowngradeDiff& diff);

}  // namespace heyp

#endif  // HEYP_ALG_DOWNGRADE_IFACE_H_
