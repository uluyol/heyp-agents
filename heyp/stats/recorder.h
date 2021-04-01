#ifndef HEYP_STATS_RECORDER_H_
#define HEYP_STATS_RECORDER_H_

#include <cstdio>

#include "absl/container/btree_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
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

  struct OptionalLatency {
    absl::string_view kind;
    absl::Duration value;
  };

  // Signature:
  // void RecorcRpc(int bufsize_bytes,
  //                absl::string_view kind, absl::Duration dur,
  //                ... more kind dur pairs)
  template <typename... Args>
  void RecordRpc(int bufsize_bytes, Args... latency_kind_value_pairs);

  void DoneStep(absl::string_view label);

  absl::Status Close();

 private:
  // Implementation of RecordRpc
  void RecordBufsize(int bufsize_bytes);
  void RecordLatency(absl::string_view kind, absl::Duration dur);
  template <typename... Args>
  void RecordLatency(absl::string_view kind, absl::Duration dur, Args... args);

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
  absl::btree_map<std::string, HdrHistogram> latency_hists_;
};

google::protobuf::RepeatedPtrField<proto::StatsRecord::LatencyStats> ToProtoLatencyStats(
    const absl::btree_map<std::string, HdrHistogram>& latency_stats);

/*** Implementation of RecordRpc  ***/

inline void StatsRecorder::RecordBufsize(int bufsize_bytes) {
  cum_num_bits_ += bufsize_bytes * 8;
  cum_num_rpcs_ += 1;
}

template <typename... Args>
inline void StatsRecorder::RecordRpc(int bufsize_bytes,
                                     Args... latency_kind_value_pairs) {
  RecordBufsize(bufsize_bytes);
  RecordLatency(latency_kind_value_pairs...);
}

inline void StatsRecorder::RecordLatency(absl::string_view kind, absl::Duration dur) {
  if (started_) {
    auto iter = latency_hists_.find(kind);
    if (iter == latency_hists_.end()) {
      bool ok;
      std::tie(iter, ok) = latency_hists_.insert(
          std::make_pair(std::string(kind), HdrHistogram(HdrHistogram::NetworkConfig())));
    }
    iter->second.RecordValue(absl::ToInt64Nanoseconds(dur));
  }
}

template <typename... Args>
inline void StatsRecorder::RecordLatency(absl::string_view kind, absl::Duration dur,
                                         Args... args) {
  RecordLatency(kind, dur);
  RecordLatency(args...);
}

}  // namespace heyp

#endif  // HEYP_STATS_RECORDER_H_
