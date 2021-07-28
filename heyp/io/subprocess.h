#ifndef HEYP_IO_SUBPROCESS_H_
#define HEYP_IO_SUBPROCESS_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/cord.h"

namespace heyp {

struct SubprocessResult {
  absl::Cord out;
  absl::Cord err;
  absl::Status status;

  bool ok() { return status.ok(); }
  absl::Status ErrorWhenRunning(absl::string_view name);
};

SubprocessResult RunSubprocess(const std::string& command,
                               const std::vector<std::string>& args,
                               absl::Cord input = {});

}  // namespace heyp

#endif  // HEYP_IO_SUBPROCESS_H_
