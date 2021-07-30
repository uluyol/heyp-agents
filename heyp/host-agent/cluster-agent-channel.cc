#include "heyp/host-agent/cluster-agent-channel.h"

#include "heyp/proto/heyp.pb.h"

namespace heyp {

// TODO: consider removing all failed and just making a new stream when something fails.

ClusterAgentChannel::ClusterAgentChannel(std::unique_ptr<proto::ClusterAgent::Stub> stub)
    : stub_(std::move(stub)),
      read_failed_(false),
      write_failed_(false),
      writes_done_(false) {}

void ClusterAgentChannel::CreateStreamIfNeeded() {
  if (stream_) {
    return;
  }
  stream_ = std::make_shared<StreamInfo>();
  stream_->stream = stub_->RegisterHost(&stream_->ctx);
  all_streams_.push_back(std::weak_ptr<StreamInfo>(stream_));
}

grpc::Status ClusterAgentChannel::CheckFinishAndResetStream() {
  if (stream_) {
    if (read_failed_ && (write_failed_ || writes_done_)) {
      auto st = stream_->stream->Finish();
      stream_ = nullptr;
      read_failed_ = false;
      write_failed_ = false;
      writes_done_ = false;
      return st;
    }
  }
  return grpc::Status();
}

grpc::Status ClusterAgentChannel::WritesDone() {
  mu_.Lock();
  bool do_write = !write_failed_ && !writes_done_;
  CreateStreamIfNeeded();
  std::shared_ptr<StreamInfo> s = stream_;
  mu_.Unlock();

  bool failed = false;
  if (do_write) {
    failed = !s->stream->WritesDone();
  }

  absl::MutexLock l(&mu_);
  write_failed_ = write_failed_ || failed;
  writes_done_ = true;
  if (write_failed_ || writes_done_) {
    if (grpc::Status st = CheckFinishAndResetStream(); !st.ok()) {
      return st;
    }
    return grpc::Status(grpc::UNKNOWN, "write failed (or done) but not read");
  }

  return grpc::Status();
}

grpc::Status ClusterAgentChannel::Write(const proto::InfoBundle& bundle) {
  mu_.Lock();
  bool do_write = !write_failed_ && !writes_done_;
  CreateStreamIfNeeded();
  std::shared_ptr<StreamInfo> s = stream_;
  mu_.Unlock();

  bool failed = false;
  if (do_write) {
    failed = !s->stream->Write(bundle);
  }

  absl::MutexLock l(&mu_);
  write_failed_ = write_failed_ || failed;
  if (write_failed_ || writes_done_) {
    if (grpc::Status st = CheckFinishAndResetStream(); !st.ok()) {
      return st;
    }
    return grpc::Status(grpc::UNKNOWN, "write failed (or done) but not read");
  }

  return grpc::Status();
}

grpc::Status ClusterAgentChannel::Read(proto::AllocBundle* bundle) {
  mu_.Lock();
  bool do_read = !read_failed_;
  CreateStreamIfNeeded();
  std::shared_ptr<StreamInfo> s = stream_;
  mu_.Unlock();

  bool failed = false;
  if (do_read) {
    failed = !s->stream->Read(bundle);
  }

  absl::MutexLock l(&mu_);
  read_failed_ = read_failed_ || failed;
  if (read_failed_) {
    if (grpc::Status st = CheckFinishAndResetStream(); !st.ok()) {
      return st;
    }
    return grpc::Status(grpc::UNKNOWN, "read failed but not write");
  }

  return grpc::Status();
}

void ClusterAgentChannel::TryCancel() {
  absl::MutexLock l(&mu_);
  for (size_t i = 0; i < all_streams_.size(); i++) {
    std::shared_ptr<StreamInfo> s = all_streams_[i].lock();
    if (s) {
      s->ctx.TryCancel();
    }
  }
}

}  // namespace heyp