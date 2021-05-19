#ifndef HEYP_IO_DEBUG_OUTPUT_LOGGER_H_
#define HEYP_IO_DEBUG_OUTPUT_LOGGER_H_

#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/time/clock.h"

namespace heyp {

class DebugOutputLogger {
 public:
  explicit DebugOutputLogger(std::string_view outdir = "");

  void Write(std::string_view data_kind, const absl::Cord& data,
             absl::Time time = absl::Now());

  bool should_log() const;
  absl::Status status() const;

 private:
  const std::string outdir_;
  absl::Status status_;
};

}  // namespace heyp

#endif  // HEYP_IO_DEBUG_OUTPUT_LOGGER_H_
