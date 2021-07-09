#ifndef HEYP_CLUSTER_AGENT_ALLOC_RECORDER_H_
#define HEYP_CLUSTER_AGENT_ALLOC_RECORDER_H_

#include <cstdio>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

// AllocRecorder writes out a sequence of newline-delimited JSON objects
// (heyp.proto.AllocRecord), each containing the inputs and outputs for a given
// allocation.
class AllocRecorder {
 public:
  static absl::StatusOr<std::unique_ptr<AllocRecorder>> Create(
      const std::string& file_path);

  explicit AllocRecorder(FILE* out);

  ~AllocRecorder();

  AllocRecorder(const AllocRecorder&) = delete;
  AllocRecorder& operator=(const AllocRecorder&) = delete;

  void Record(absl::Time time, const proto::AggInfo& info,
              const std::vector<proto::FlowAlloc>& allocs);

  absl::Status Close();

 private:
  FILE* out_;
  absl::Status write_status_;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_ALLOC_RECORDER_H_
