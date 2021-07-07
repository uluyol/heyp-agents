#include "heyp/host-agent/linux-enforcer/small-string-set.h"

namespace heyp {

SmallStringSet::SmallStringSet(const std::vector<absl::string_view>& entries) {
  size_t bufsize = 0;
  for (absl::string_view sv : entries) {
    bufsize += sv.size();
  }
  buf_.reset(new char[bufsize]);

  size_t start = 0;
  data_.resize(entries.size(), "");
  for (size_t i = 0; i < entries.size(); ++i) {
    memcpy(buf_.get() + start, entries[i].data(), entries[i].size());
    data_[i] = absl::string_view(buf_.get() + start, entries[i].size());
    start += entries[i].size();
  }
}

}  // namespace heyp
