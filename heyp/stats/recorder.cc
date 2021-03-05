#include "heyp/stats/recorder.h"

#include "absl/memory/memory.h"
#include "absl/time/clock.h"
#include "glog/logging.h"
#include "google/protobuf/util/json_util.h"
#include "heyp/posix/strerror.h"

namespace heyp {

absl::StatusOr<std::unique_ptr<StatsRecorder>> StatsRecorder::Create(
    const std::string& file_path) {
  FILE* f = fopen(file_path.c_str(), "w");
  if (f == nullptr) {
    return absl::InternalError(StrError(errno));
  }

  return absl::make_unique<StatsRecorder>(f);
}

StatsRecorder::StatsRecorder(FILE* out)
    : out_(out),
      executor_(1),
      started_(false),
      latency_hist_(HdrHistogram::NetworkConfig()) {}

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

void StatsRecorder::RecordRpc(int bufsize_bytes, absl::Duration latency) {
  cum_num_bits_ += bufsize_bytes * 8;
  cum_num_rpcs_ += 1;
  if (started_) {
    latency_hist_.RecordValue(absl::ToInt64Nanoseconds(latency));
  }
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
  proto::HdrHistogram proto_hist = latency_hist_.ToProto();
  FILE* fout = out_;

  std::array perc_latencies{
      latency_hist_.ValueAtPercentile(50),
      latency_hist_.ValueAtPercentile(90),
      latency_hist_.ValueAtPercentile(95),
      latency_hist_.ValueAtPercentile(99),
  };

  // wait for any previous work
  if (prev_tg_ != nullptr) {
    write_status_.Update(prev_tg_->WaitAll());
  }

  prev_tg_ = executor_.NewTaskGroup();
  prev_tg_->AddTask([label_str, now, elapsed_sec, cum_num_bits, cum_num_rpcs, mean_bps,
                     mean_rpcps, proto_hist, perc_latencies, fout]() -> absl::Status {
    proto::StatsRecord rec;

    rec.set_label(label_str);
    rec.set_timestamp(absl::FormatTime(now));
    rec.set_dur_sec(elapsed_sec);

    rec.set_cum_num_bits(cum_num_bits);
    rec.set_cum_num_rpcs(cum_num_rpcs);

    rec.set_mean_bits_per_sec(mean_bps);
    rec.set_mean_rpcs_per_sec(mean_rpcps);
    *rec.mutable_latency_ns_hist() = proto_hist;

    rec.set_latency_ns_p50(perc_latencies[0]);
    rec.set_latency_ns_p90(perc_latencies[1]);
    rec.set_latency_ns_p95(perc_latencies[2]);
    rec.set_latency_ns_p99(perc_latencies[3]);

    google::protobuf::util::JsonPrintOptions opt;
    opt.add_whitespace = false;
    opt.always_print_primitive_fields = true;
    opt.always_print_enums_as_ints = false;

    std::string out;
    auto st = google::protobuf::util::MessageToJsonString(rec, &out, opt);
    if (!st.ok()) {
      return absl::Status(static_cast<absl::StatusCode>(st.code()),
                          std::string(st.message()));
    }

    if (fwrite(out.data(), out.size(), 1, fout) != out.size()) {
      return absl::InternalError(StrError(errno));
    }
    if (fwrite("\n", 1, 1, fout) != 1) {
      return absl::InternalError(StrError(errno));
    }
    return absl::OkStatus();
  });
  prev_time_ = now;
  prev_cum_num_bits_ = cum_num_bits_;
  prev_cum_num_rpcs_ = cum_num_rpcs_;
  latency_hist_.Reset();
}

}  // namespace heyp
