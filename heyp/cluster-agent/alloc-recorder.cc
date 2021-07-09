#include "heyp/cluster-agent/alloc-recorder.h"

#include "absl/time/clock.h"
#include "heyp/log/logging.h"
#include "heyp/posix/strerror.h"
#include "heyp/proto/fileio.h"

namespace heyp {

absl::StatusOr<std::unique_ptr<AllocRecorder>> AllocRecorder::Create(
    const std::string& file_path) {
  FILE* f = fopen(file_path.c_str(), "w");
  if (f == nullptr) {
    return absl::InternalError(StrError(errno));
  }

  return absl::make_unique<AllocRecorder>(f);
}

AllocRecorder::AllocRecorder(FILE* out) : out_(out) {}

AllocRecorder::~AllocRecorder() {
  if (out_ != nullptr) {
    LOG(FATAL) << "AllocRecorder: must call Close";
  }
}

absl::Status AllocRecorder::Close() {
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

void AllocRecorder::Record(absl::Time time, const proto::AggInfo& info,
                           const std::vector<proto::FlowAlloc>& allocs) {
  proto::DebugAllocRecord rec;
  rec.set_timestamp(absl::FormatTime(time, absl::UTCTimeZone()));
  *rec.mutable_info() = info;
  *rec.mutable_flow_allocs() = {allocs.begin(), allocs.end()};
  write_status_.Update(WriteJsonLine(rec, out_));
  if (write_status_.ok()) {
    if (fflush(out_)) {
      write_status_.Update(absl::InternalError(
          std::string("failed to flush alloc records file: ") + StrError(errno)));
    }
  }
}

}  // namespace heyp
