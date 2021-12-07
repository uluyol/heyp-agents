#include "heyp/alg/downgrade/hash-ring.h"

#include "absl/strings/str_cat.h"

namespace heyp {

std::string ToString(const RingRanges& r) {
  return absl::StrCat("{ a = ", ToString(r.a), ", b = ", ToString(r.b), "}");
}

std::ostream& operator<<(std::ostream& os, const RingRanges& r) {
  return os << ToString(r);
}

std::string ToString(RangeDiffType t) {
  switch (t) {
    case RangeDiffType::kAdd:
      return "kAdd";
    case RangeDiffType::kDel:
      return "kDel";
  }
  return "unknown";
}

std::string ToString(const RangeDiff& d) {
  return absl::StrCat("{ diff = ", ToString(d.diff), ", type = ", ToString(d.type), "}");
}

std::ostream& operator<<(std::ostream& os, const RangeDiff& d) {
  return os << ToString(d);
}

std::string HashRing::ToString() const {
  return absl::StrCat("{ start = ", start_, ", frac = ", frac_, "}");
}

}  // namespace heyp