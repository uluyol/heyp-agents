#include "heyp/posix/pidfile.h"

#include <fcntl.h>
#include <unistd.h>

#include "absl/strings/str_cat.h"
#include "heyp/posix/strerror.h"

namespace heyp {

absl::Status WritePidFile(const std::string& path) {
  int fd = creat(path.c_str(), 0644);
  if (fd == -1) {
    return absl::InternalError(
        absl::StrCat("failed to create ", path, ": ", StrError(errno)));
  }

  pid_t pid = getpid();
  std::string pid_str = absl::StrCat(pid, "\n");

  ssize_t written = 0;
  while (written != pid_str.size()) {
    ssize_t wrote = write(fd, pid_str.data() + written, pid_str.size() - written);
    if (wrote == -1) {
      close(fd);
      return absl::InternalError(
          absl::StrCat("failed to write ", path, ": ", StrError(errno)));
    }
    written += wrote;
  }

  if (close(fd) == -1) {
    return absl::InternalError(
        absl::StrCat("failed to write ", path, ": ", StrError(errno)));
  }

  return absl::OkStatus();
}

}  // namespace heyp
