#include "heyp/alg/downgrade/hash-ring.h"

#include "absl/strings/str_cat.h"

namespace heyp {

std::string HashRing::ToString() const {
  return absl::StrCat("{ start = ", start_, ", frac = ", frac_, "}");
}

}  // namespace heyp