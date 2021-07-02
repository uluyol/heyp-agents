#ifndef HEYP_PROTO_NDJSON_LOGGER_H_
#define HEYP_PROTO_NDJSON_LOGGER_H_

#include <cstdio>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "heyp/proto/fileio.h"

namespace heyp {

class NdjsonLogger {
 public:
  explicit NdjsonLogger(FILE* out);
  ~NdjsonLogger();

  NdjsonLogger(const NdjsonLogger&) = delete;
  NdjsonLogger& operator=(const NdjsonLogger&) = delete;

  // Init re-initializes the Logger with the provided output file.
  // Call Close before Init in case the NdjsonLogger has an open output file.
  absl::Status Init(const std::string& file_path);
  void Init(FILE* out);

  absl::Status Write(const google::protobuf::Message& record);

  absl::Status Close();

  bool should_log() const { return out_ != nullptr; }

 private:
  FILE* out_;
};

absl::StatusOr<std::unique_ptr<NdjsonLogger>> CreateNdjsonLogger(
    const std::string& file_path);

}  // namespace heyp

#endif  // HEYP_PROTO_NDJSON_LOGGER_H_
