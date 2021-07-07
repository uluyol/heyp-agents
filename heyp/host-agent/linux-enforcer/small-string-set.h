#ifndef HEYP_HOST_AGENT_LINUX_ENFORCER_SMALL_STRING_SET_H_
#define HEYP_HOST_AGENT_LINUX_ENFORCER_SMALL_STRING_SET_H_

#include <algorithm>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"

namespace heyp {

class SmallStringSet {
 public:
  explicit SmallStringSet(const std::vector<absl::string_view>& entries);

  bool contains(absl::string_view value) const {
    return std::find(data_.begin(), data_.end(), value) != data_.end();
  }

 private:
  absl::InlinedVector<absl::string_view, 2> data_;
  std::unique_ptr<char[]> buf_;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_LINUX_ENFORCER_SMALL_STRING_SET_H_