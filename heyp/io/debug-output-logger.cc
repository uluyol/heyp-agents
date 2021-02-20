#include "heyp/io/debug-output-logger.h"

#include <cstdio>
#include <filesystem>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "heyp/posix/strerror.h"

namespace heyp {

bool DebugOutputLogger::should_log() const { return !outdir_.empty(); }

DebugOutputLogger::DebugOutputLogger(std::string_view outdir) : outdir_(outdir) {
  if (!should_log()) {
    return;
  }

  std::error_code err_code;
  bool ok = std::filesystem::create_directories(outdir, err_code);
  if (err_code) {
    status_ = absl::InternalError(err_code.message());
  } else if (!ok) {
    status_ =
        absl::UnknownError(absl::StrCat("failed to create output directory: ", outdir));
  }
}

void DebugOutputLogger::Write(std::string_view data_kind, const absl::Cord &data,
                              absl::Time time) {
  if (!should_log()) {
    return;
  }
  std::string out_path = absl::StrCat(
      outdir_, "/", absl::FormatTime(time, absl::UTCTimeZone()), "-", data_kind);
  FILE *f = fopen(out_path.c_str(), "w");
  if (f == nullptr && status_.ok()) {
    status_ = absl::InternalError(
        absl::StrCat("failed to create file '", out_path, "': ", StrError(errno)));
  }
  auto cleanup = absl::MakeCleanup([f] { fclose(f); });

  for (absl::string_view chunk : data.Chunks()) {
    if (!fwrite(chunk.data(), 1, chunk.size(), f)) {
      status_ =
          absl::InternalError(absl::StrCat("failure while writing '", out_path, "'"));
      return;
    }
  }
}

absl::Status DebugOutputLogger::status() const { return status_; }

}  // namespace heyp
