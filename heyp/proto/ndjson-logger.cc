#include "heyp/proto/ndjson-logger.h"

#include "absl/memory/memory.h"
#include "heyp/log/logging.h"
#include "heyp/posix/strerror.h"

namespace heyp {

NdjsonLogger::NdjsonLogger(FILE* out) : out_(out) {}

NdjsonLogger::~NdjsonLogger() {
  if (out_ != nullptr) {
    LOG(WARNING) << "NdjsonLogger: should call close";
    fclose(out_);
  }
}

void NdjsonLogger::Init(FILE* out) { out_ = out; }

absl::Status NdjsonLogger::Init(const std::string& path) {
  FILE* f = fopen(path.c_str(), "w");
  if (f == nullptr) {
    return absl::InternalError(StrError(errno));
  }
  Init(f);
  return absl::OkStatus();
}

absl::Status NdjsonLogger::Close() {
  int ret = 0;
  if (out_ != nullptr) {
    ret = fclose(out_);
    out_ = nullptr;
  }

  if (ret != 0) {
    return absl::InternalError("failed to close output file");
  }
  return absl::OkStatus();
}

absl::Status NdjsonLogger::Write(const google::protobuf::Message& record) {
  return WriteJsonLine(record, out_);
}

absl::StatusOr<std::unique_ptr<NdjsonLogger>> CreateNdjsonLogger(
    const std::string& file_path) {
  auto logger = absl::make_unique<NdjsonLogger>(nullptr);
  absl::Status st = logger->Init(file_path);
  if (!st.ok()) {
    return st;
  }
  return logger;
}

}  // namespace heyp