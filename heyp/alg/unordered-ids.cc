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

std::string ToString(const UnorderedIds& set, std::string_view indent) {
  return absl::StrCat("{\n", indent, "  ranges = (",
                      absl::StrJoin(set.ranges, ", ", IdRangeFormatter()), "),\n", indent,
                      "  points = (", absl::StrJoin(set.points, ", "), "),\n", indent,
                      "}");
}

std::ostream& operator<<(std::ostream& os, IdRange range) {
  return os << ToString(range);
}

std::ostream& operator<<(std::ostream& os, const UnorderedIds& set) {
  return os << ToString(set);
}

bool operator==(const UnorderedIds& lhs, const UnorderedIds& rhs) {
  return (lhs.ranges == rhs.ranges) && (lhs.points == rhs.points);
}

}  // namespace heyp
