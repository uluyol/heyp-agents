#include "heyp/stats/recorder.h"

#include "absl/memory/memory.h"
#include "absl/time/clock.h"
#include "heyp/log/logging.h"
#include "heyp/posix/strerror.h"
#include "heyp/proto/fileio.h"

namespace heyp {

absl::StatusOr<std::unique_ptr<StatsRecorder>> StatsRecorder::Create(
    const std::string& file_path) {
  FILE* f = fopen(file_path.c_str(), "w");
  if (f == nullptr) {
    return absl::InternalError(StrError(errno));
  }

  return absl::make_unique<StatsRecorder>(f);
}

StatsRecorder::StatsRecorder(FILE* out) : out_(out), executor_(1), started_(false) {}

StatsRecorder::~StatsRecorder() {
  if (out_ != nullptr) {
    LOG(FATAL) << "StatsRecorder: must call Close";
  }
}

absl::Status StatsRecorder::Close() {
  if (prev_tg_ != nullptr) {
    write_status_.Update(prev_tg_->WaitAll());
    prev_tg_ = nullptr;
  }

  int ret = 0;
  if (out_ != nullptr) {
    ret = fclose(out_);
    out_ = nullptr;
  }

  absl::Status st = write_status_;
  if (ret != 0) {
    st.Update(absl::InternalError("failed to close output file"));
  }
  return st;
}

void StatsRecorder::StartRecording() {
  started_ = true;
  prev_time_ = absl::Now();
}

google::protobuf::RepeatedPtrField<proto::StatsRecord::LatencyStats> ToProtoLatencyStats(
    const absl::btree_map<std::string, HdrHistogram>& latency_stats) {
  google::protobuf::RepeatedPtrField<proto::StatsRecord::LatencyStats> result;
  result.Reserve(latency_stats.size());
  for (auto p : latency_stats) {
    proto::StatsRecord::LatencyStats* s = result.Add();
    s->set_kind(p.first);
    *s->mutable_hist_ns() = p.second.ToProto();
    s->set_p50_ns(p.second.ValueAtPercentile(50));
    s->set_p90_ns(p.second.ValueAtPercentile(90));
    s->set_p95_ns(p.second.ValueAtPercentile(95));
    s->set_p99_ns(p.second.ValueAtPercentile(99));
  }
  return result;
}

void StatsRecorder::DoneStep(absl::string_view label) {
  CHECK(started_);

  // copy for async writing
  std::string label_str(label);
  absl::Time now = absl::Now();
  double elapsed_sec = absl::ToDoubleSeconds(now - prev_time_);
  double mean_bps = (cum_num_bits_ - prev_cum_num_bits_) / elapsed_sec;
  int64_t mean_rpcps = (cum_num_rpcs_ - prev_cum_num_rpcs_) / elapsed_sec;
  int64_t cum_num_bits = cum_num_bits_;
  int64_t cum_num_rpcs = cum_num_rpcs_;

  auto latency_data = ToProtoLatencyStats(latency_hists_);
  FILE* fout = out_;

  // wait for any previous work
  if (prev_tg_ != nullptr) {
    write_status_.Update(prev_tg_->WaitAll());
  }

  prev_tg_ = executor_.NewTaskGroup();
  prev_tg_->AddTask([label_str, now, elapsed_sec, cum_num_bits, cum_num_rpcs, mean_bps,
                     mean_rpcps, latency_data, fout]() -> absl::Status {
    proto::StatsRecord rec;

    rec.set_label(label_str);
    rec.set_timestamp(absl::FormatTime(now, absl::UTCTimeZone()));
    rec.set_dur_sec(elapsed_sec);

    rec.set_cum_num_bits(cum_num_bits);
    rec.set_cum_num_rpcs(cum_num_rpcs);

    rec.set_mean_bits_per_sec(mean_bps);
    rec.set_mean_rpcs_per_sec(mean_rpcps);
    *rec.mutable_latency() = latency_data;

    return WriteJsonLine(rec, fout);
  });
  prev_time_ = now;
  prev_cum_num_bits_ = cum_num_bits_;
  prev_cum_num_rpcs_ = cum_num_rpcs_;
  for (auto iter : latency_hists_) {
    iter.second.Reset();
  }
}

}  // namespace heyp
