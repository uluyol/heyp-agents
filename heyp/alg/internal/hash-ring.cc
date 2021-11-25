#include "heyp/alg/internal/hash-ring.h"

#include "absl/strings/str_cat.h"

namespace heyp {
namespace internal {

std::string HashRing::ToString() const {
  return absl::StrCat("{ start = ", start_, ", frac = ", frac_, "}");
}

}  // namespace internal
}  // namespace heyp