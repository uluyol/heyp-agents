#ifndef HEYP_STATS_RECORDER_H_
#define HEYP_STATS_RECORDER_H_

#include <cstdio>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "heyp/stats/hdrhistogram.h"
#include "heyp/threads/executor.h"

namespace heyp {

// StatsRecorder writes out a sequence of newline-delimited JSON objects
// (heyp.proto.StatsRecord), each containing the stats for a given step.
class StatsRecorder {
 public:
  static absl::StatusOr<std::unique_ptr<StatsRecorder>> Create(
      const std::string& file_path);

  explicit StatsRecorder(FILE* out);

  ~StatsRecorder();

  StatsRecorder(const StatsRecorder&) = delete;
  StatsRecorder& operator=(const StatsRecorder&) = delete;

  void StartRecording();
  void RecordRpc(int bufsize_bytes, absl::Duration latency);
  void DoneStep(absl::string_view label);

  absl::Status Close();

 private:
  FILE* out_;
  Executor executor_;
  absl::Status write_status_;
  bool started_;

  std::unique_ptr<TaskGroup> prev_tg_;
  absl::Time prev_time_;
  int64_t prev_cum_num_bits_;
  int64_t prev_cum_num_rpcs_;

  int64_t cum_num_bits_;
  int64_t cum_num_rpcs_;
  HdrHistogram latency_hist_;
};

}  // namespace heyp

#endif  // HEYP_STATS_RECORDER_H_
