#include "heyp/proto/ndjson-logger.h"

#include <fcntl.h>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "heyp/posix/strerror.h"

namespace heyp {

NdjsonLogger::NdjsonLogger(int fd) : fd_(fd), logger_(MakeLogger("ndjson-logger")) {}

NdjsonLogger::~NdjsonLogger() {
  if (fd_ != -1) {
    SPDLOG_LOGGER_WARN(&logger_, "should call close");
    int ret;
    do {
      ret = close(fd_);
    } while (ret != 0 && errno == EINTR);
    if (ret != 0) {
      SPDLOG_LOGGER_WARN(&logger_, "failed to close output file: {}", StrError(errno));
    }
  }
}

void NdjsonLogger::Init(int fd) { fd_ = fd; }

absl::Status NdjsonLogger::Init(const std::string& path) {
  int fd;
  do {
    fd = creat(path.c_str(), 0644);
  } while (fd == -1 && errno == EINTR);
  if (fd == -1) {
    return absl::InternalError(StrError(errno));
  }
  Init(fd);
  return absl::OkStatus();
}

absl::Status NdjsonLogger::Close() {
  if (fd_ == -1) {
    return absl::OkStatus();
  }

  int ret;
  do {
    ret = close(fd_);
  } while (ret != 0 && errno == EINTR);
  if (ret != 0) {
    return absl::InternalError(
        absl::StrCat("failed to close output file: ", StrError(errno)));
  }
  return absl::OkStatus();
}

absl::Status NdjsonLogger::Write(const google::protobuf::Message& record) {
  if (fd_ == -1) {
    return absl::FailedPreconditionError(
        "have null output file. Did you call NdjsonLogger::Close?");
  }

  auto status = WriteJsonLine(record, fd_);
  if (!status.ok()) {
    return absl::Status(status.code(),
                        absl::StrCat("failed to write record: ", status.message()));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<NdjsonLogger>> CreateNdjsonLogger(
    const std::string& file_path) {
  auto logger = absl::make_unique<NdjsonLogger>(-1);
  absl::Status st = logger->Init(file_path);
  if (!st.ok()) {
    return st;
  }
  return logger;
}

}  // namespace heyp
