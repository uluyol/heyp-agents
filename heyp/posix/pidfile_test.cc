#include "heyp/posix/pidfile.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"
#include "heyp/io/file.h"

int main() {
  pid_t pid = getpid();

  std::string tmpdir = getenv("TEST_TMPDIR");
  if (tmpdir.empty()) {
    tmpdir = ".";
  }

  std::string pidfile = absl::StrCat(tmpdir, "/pid");
  absl::Status st = heyp::WritePidFile(pidfile);
  if (!st.ok()) {
    absl::FPrintF(stderr, "failed to write pid file: %s\n", st.message());
    return 1;
  }

  std::string got_pid;
  st = file::GetContents(pidfile, &got_pid, file::Defaults());
  if (!st.ok()) {
    absl::FPrintF(stderr, "failed to read pid file: %s\n", st.message());
    return 1;
  }

  if (got_pid.size() > 16) {
    absl::FPrintF(stderr, "pid file is too long: %s\n", got_pid);
    return 1;
  }

  pid_t got_pid_num;
  got_pid = absl::StripAsciiWhitespace(got_pid);
  if (!absl::SimpleAtoi(got_pid, &got_pid_num)) {
    absl::FPrintF(stderr, "failed to parse pid in %s\n", got_pid);
    return 1;
  }

  if (pid != got_pid_num) {
    absl::FPrintF(stderr, "pids don't match: got %d want %d\n", got_pid_num, pid);
    return 1;
  }

  return 0;
}
