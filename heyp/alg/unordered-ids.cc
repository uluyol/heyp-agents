#include "heyp/alg/unordered-ids.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace heyp {
namespace {
struct IdRangeFormatter {
  void operator()(std::string* out, IdRange range) {
    absl::StrAppend(out, "[", range.lo, ", ", range.hi, "]");
  }
};
}  // namespace

std::string ToString(IdRange range) {
  std::string out;
  IdRangeFormatter()(&out, range);
  return out;
}

std::string ToString(const UnorderedIds& set) {
  return absl::StrCat("{\n  ranges = (",
                      absl::StrJoin(set.ranges, ", ", IdRangeFormatter()),
                      "),\n  points = (", absl::StrJoin(set.points, ", "), "),\n}");
}

std::ostream& operator<<(std::ostream& os, IdRange range) {
  return os << ToString(range);
}

std::ostream& operator<<(std::ostream& os, const UnorderedIds& set) {
  return os << ToString(set);
}

}  // namespace heyp
