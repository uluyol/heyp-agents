#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <uv.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <list>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/functional/bind_front.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "heyp/encoding/binary.h"
#include "heyp/host-agent/urls.h"
#include "heyp/init/init.h"
#include "heyp/posix/strerror.h"
#include "heyp/proto/app.pb.h"
#include "heyp/proto/fileio.h"
#include "heyp/stats/recorder.h"

namespace heyp {
namespace {

// LOG_TO_CERR, if set, will cause std::cerr to be used for logging instead of glog.
//#define LOG_TO_CERR
#undef LOG_TO_CERR

#ifdef LOG_TO_CERR
#define WITH_NEWLINE << "\n"
#define SHARD_LOG(level) std::cerr << "shard " << ::heyp::ThisShardIndex << ": "
#else
#define WITH_NEWLINE
#define SHARD_LOG(level) LOG(level) << "shard " << ::heyp::ThisShardIndex << ": "
#endif

constexpr bool kDebugReadWrite = false;
constexpr bool kLimitedDebugReadWrite = false;
constexpr bool kDebugPool = false;
constexpr bool kDebugLoadGen = false;

int ThisShardIndex = 0;  // Initialized by ShardMain

bool InLimitedDebugReadWrite() {
  static int32_t LimitedDebugReadWriteCounter = 0;
  return kLimitedDebugReadWrite && LimitedDebugReadWriteCounter++ <= 300;
}

void AllocBuf(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  buf->base = new char[suggested_size];
  buf->len = suggested_size;
}

std::string IP4Name(const struct sockaddr_in* src) {
  char buf[24];
  memset(buf, 0, 24);
  uv_ip4_name(src, buf, 24);
  return std::string(buf);
}

uint64_t UvTimeoutUntil(uv_loop_t* loop, uint64_t hr_time) {
  uint64_t now = uv_now(loop);
  if (now >= hr_time / 1'000'000) {
    return 0;
  }
  return (hr_time / 1'000'000) - now;
}

class ClientConn;
struct ClientConnOptions {
  uv_loop_t* loop;
  StatsRecorder* stats_recorder;
  std::vector<std::pair<uint64_t, uint64_t>>* goodput_ts;
  int32_t max_rpc_size_bytes;
  uint64_t hr_connect_deadline;
  std::function<void()> on_pool_init;
  std::function<void()> on_write_done;
};

class ClientConnPool {
 public:
  ClientConnPool(const ClientConnOptions& conn_options, int num_conns,
                 std::vector<struct sockaddr_in> dst_addrs);

  void MarkConnInitialized();
  bool AllConnsInitialized() const;

  void WithConn(const std::function<void(ClientConn*)>& func);

 private:
  const std::vector<struct sockaddr_in> dst_addrs_;
  std::vector<std::unique_ptr<ClientConn>> all_;
  int num_init_;
};

uint64_t HighResTimeSub(uint64_t end, uint64_t start) {
  if (start > end) {
    return 0;
  }
  return end - start;
}

class ClientConn {
 public:
  ClientConn(const ClientConnOptions& o, int conn_id, const struct sockaddr_in* addr,
             ClientConnPool* pool)
      : options_(o),
        pool_(pool),
        conn_id_(conn_id),
        addr_(addr),
        buf_(options_.max_rpc_size_bytes, 0),
        num_seen_(0),
        num_issued_(0),
        read_buf_size_(0) {
    CHECK_GE(options_.max_rpc_size_bytes, 28);

    uv_tcp_init(options_.loop, &client_);
    client_.data = this;

    if (kDebugPool) {
      SHARD_LOG(INFO) << "requesting connection to " << IP4Name(addr_) WITH_NEWLINE;
    }
    uv_connect_t* req = new uv_connect_t();

    uv_tcp_connect(req, &client_, reinterpret_cast<const struct sockaddr*>(addr_),
                   [](uv_connect_t* req, int status) {
                     auto self = reinterpret_cast<ClientConn*>(req->handle->data);
                     self->OnConnect(req, status);
                   });
  }

  void IssueRpc(uint64_t rpc_id, uint64_t hr_scheduled_time, int32_t rpc_size_bytes) {
    ++num_issued_;
    WriteU32LE(rpc_size_bytes - 28, buf_.data());
    WriteU64LE(rpc_id, buf_.data() + 4);
    WriteU64LE(hr_scheduled_time, buf_.data() + 12);
    // We might issue slightly early to account for clock precision.
    uint64_t hr_issue_time = std::max(hr_scheduled_time, uv_hrtime());
    WriteU64LE(hr_issue_time, buf_.data() + 20);
    if (kDebugReadWrite || InLimitedDebugReadWrite()) {
      SHARD_LOG(INFO) << "write(c=" << conn_id_ << ") rpc id=" << rpc_id
                      << " header=" << ToHex(buf_.data(), 28) WITH_NEWLINE;
    }
    buffer_ = uv_buf_init(buf_.data(), rpc_size_bytes);
    uv_write_t* req = new uv_write_t();
    if (kDebugReadWrite || kLimitedDebugReadWrite) {
      req->data = reinterpret_cast<void*>(static_cast<uintptr_t>(rpc_id));
    }
    has_pending_write_ = true;
    uv_write(req, reinterpret_cast<uv_stream_t*>(&client_), &buffer_, 1,
             [](uv_write_t* req, int status) {
               auto self = reinterpret_cast<ClientConn*>(req->handle->data);
               self->OnWriteDone(req, status);
             });
  }

  void AssertValid() const { CHECK_LE(num_seen_, num_issued_); }

 private:
  void OnConnect(uv_connect_t* req, int status) {
    if (uv_hrtime() > options_.hr_connect_deadline) {
      SHARD_LOG(FATAL) << "took too long to connect" WITH_NEWLINE;
    }

    AssertValid();

    if (status < 0) {
      SHARD_LOG(ERROR) << "failed to connect (" << uv_strerror(status)
                       << "); trying again" WITH_NEWLINE;
      uv_tcp_init(options_.loop, &client_);
      client_.data = this;

      usleep(rand() % 50000);

      uv_tcp_connect(req, &client_, reinterpret_cast<const struct sockaddr*>(addr_),
                     [](uv_connect_t* req, int status) {
                       auto self = reinterpret_cast<ClientConn*>(req->handle->data);
                       self->OnConnect(req, status);
                     });
      return;
    }

    SHARD_LOG(INFO) << "connection established; adding to pool" WITH_NEWLINE;
    uv_read_start(reinterpret_cast<uv_stream_t*>(&client_), AllocBuf,
                  [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
                    auto self = reinterpret_cast<ClientConn*>(stream->data);
                    self->OnReadAck(nread, buf);
                  });

    pool_->MarkConnInitialized();
    if (pool_->AllConnsInitialized()) {
      if (kDebugPool) {
        SHARD_LOG(INFO) << "connection pool is fully initialized" WITH_NEWLINE;
      }
      options_.on_pool_init();
    }

    delete req;
  }

  void OnReadAck(ssize_t nread, const uv_buf_t* buf) {
    AssertValid();
    if (nread < 0) {
      if (nread != UV_EOF) absl::FPrintF(stderr, "Read error %s\n", uv_err_name(nread));
      uv_read_stop(reinterpret_cast<uv_stream_t*>(&client_));
      return;
    }

    bool got_ack = false;
    auto record_rpc_latency = [&](uint32_t size, uint64_t rpc_id,
                                  uint64_t got_hr_scheduled_time,
                                  uint64_t got_hr_client_write_time) {
      uint64_t hr_now = uv_hrtime();
      options_.stats_recorder->RecordRpc(
          size, "full", absl::Nanoseconds(HighResTimeSub(hr_now, got_hr_scheduled_time)),
          "net", absl::Nanoseconds(HighResTimeSub(hr_now, got_hr_client_write_time)));
      if (options_.goodput_ts != nullptr) {
        IncrementGoodput(hr_now);
      }
      ++num_seen_;
      got_ack = true;
    };

    char* b = buf->base;
    char* e = buf->base + nread;
    while (b < e) {
      int tocopy = std::min<int>(e - b, 28 - read_buf_size_);
      memmove(read_buf_ + read_buf_size_, b, tocopy);
      b += tocopy;
      read_buf_size_ += tocopy;

      if (read_buf_size_ == 28) {
        if (kDebugReadWrite || InLimitedDebugReadWrite()) {
          SHARD_LOG(INFO) << "read (c=" << conn_id_ << ") rpc id=" << ReadU64LE(read_buf_)
                          << " header=" << ToHex(read_buf_, 28) WITH_NEWLINE;
        }
        record_rpc_latency(28 + ReadU32LE(read_buf_), ReadU64LE(read_buf_ + 4),
                           ReadU64LE(read_buf_ + 12), ReadU64LE(read_buf_ + 20));
        read_buf_size_ = 0;
      }
    }
    delete buf->base;
  }

  void IncrementGoodput(uint64_t hr_now) {
    if (options_.goodput_ts->empty()) {
      options_.goodput_ts->push_back({hr_now, 1});
    }
    while (hr_now > options_.goodput_ts->back().first + 1'000'000) {
      std::pair<uint64_t, uint64_t> last = options_.goodput_ts->back();
      options_.goodput_ts->push_back({last.first + 1'000'000, 0});
    }
    options_.goodput_ts->back().second++;
  }

  void OnWriteDone(uv_write_t* req, int status) {
    AssertValid();
    if (status < 0) {
      absl::FPrintF(stderr, "write error %s\n", uv_strerror(status));
      --num_issued_;
      delete req;
      return;
    }

    if (kDebugReadWrite || InLimitedDebugReadWrite()) {
      uint64_t rpc_id = reinterpret_cast<uintptr_t>(req->data);
      SHARD_LOG(INFO) << "write(c=" << conn_id_ << ") rpc id=" << rpc_id
                      << " done" WITH_NEWLINE;
    }

    has_pending_write_ = false;
    delete req;
    if (options_.on_write_done) {
      options_.on_write_done();
    }
  }

  ClientConnOptions options_;
  ClientConnPool* pool_;

  const int conn_id_;
  const struct sockaddr_in* addr_;

  uv_tcp_t client_;
  uv_buf_t buffer_;
  std::string buf_;
  int num_seen_;
  int num_issued_;

  char read_buf_[28];
  int read_buf_size_;

  bool has_pending_write_ = false;

  friend class ClientConnPool;
};

ClientConnPool::ClientConnPool(const ClientConnOptions& conn_options, int num_conns,
                               std::vector<struct sockaddr_in> dst_addrs)
    : dst_addrs_(std::move(dst_addrs)), num_init_(0) {
  // Start by creating connections.
  // Once all are created, the last will register a timer to start the run
  if (kDebugPool) {
    SHARD_LOG(INFO) << "Starting " << num_conns << " connections" WITH_NEWLINE;
  }
  for (int i = 0; i < num_conns; ++i) {
    all_.push_back(absl::make_unique<ClientConn>(
        conn_options, all_.size(), &dst_addrs_.at(i % dst_addrs_.size()), this));
  }
}

void ClientConnPool::MarkConnInitialized() { ++num_init_; }
bool ClientConnPool::AllConnsInitialized() const { return num_init_ == all_.size(); }

void ClientConnPool::WithConn(const std::function<void(ClientConn*)>& func) {
  int min_waiting = std::numeric_limits<int>::max();
  ClientConn* best = nullptr;

  for (auto& c : all_) {
    int t = c->num_issued_ - c->num_seen_;
    if (min_waiting > t) {
      min_waiting = t;
      best = c.get();
    }
  }

  CHECK_NE(best, nullptr);
  func(best);
}

proto::HdrHistogram::Config InterarrivalConfig() {
  proto::HdrHistogram::Config c;
  c.set_highest_trackable_value(1'000'000'000);
  c.set_lowest_discernible_value(100);
  c.set_significant_figures(2);
  return c;
}

struct WorkloadStage {
  constexpr static int kInterarrivalSamples = 10'000;
  int32_t rpc_size_bytes;
  double target_average_bps;
  std::unique_ptr<std::array<uint64_t, kInterarrivalSamples>> interarrival_ns;
  uint64_t hr_cum_run_dur;
};

double MeanOf(const std::array<uint64_t, WorkloadStage::kInterarrivalSamples>& dist) {
  constexpr int kChunkSize = 100;
  constexpr int kNumChunks = WorkloadStage::kInterarrivalSamples / kChunkSize;
  CHECK_EQ(kChunkSize * kNumChunks, WorkloadStage::kInterarrivalSamples);
  std::array<double, kNumChunks> partial_sums;
  memset(partial_sums.data(), 0, partial_sums.size() * sizeof(double));
  for (int i = 0; i < kNumChunks; ++i) {
    for (int j = i * kChunkSize; j < (i + 1) * kChunkSize; ++j) {
      double prev = partial_sums[i];
      CHECK_GE(dist[j], 0);
      partial_sums[i] += dist[j];
      CHECK(!isnan(partial_sums[i]))
          << "got nan partial_sum: i: " << i << " j: " << j
          << " value: " << partial_sums[i] << " prev: " << prev << " inc: " << dist[j];
    }
    partial_sums[i] /= kChunkSize;
  }
  double total = 0;
  for (double s : partial_sums) {
    total += s;
  }
  return total / kNumChunks;
}

std::vector<WorkloadStage> StagesFromConfig(const proto::TestLopriClientConfig& config) {
  absl::InsecureBitGen rng;
  std::vector<WorkloadStage> stages;
  uint64_t hr_cum_run_dur = 0;
  stages.reserve(config.workload_stages_size());
  for (const auto& p : config.workload_stages()) {
    double rpcs_per_sec = p.target_average_bps() / (8 * p.rpc_size_bytes());
    SHARD_LOG(INFO) << "stage " << stages.size() << ": will target an average of "
                    << rpcs_per_sec << " rpcs/sec" WITH_NEWLINE;

    auto interarrival_ns =
        absl::make_unique<std::array<uint64_t, WorkloadStage::kInterarrivalSamples>>();
    switch (p.interarrival_dist()) {
      case proto::DIST_CONSTANT: {
        uint64_t v = 1e9 / rpcs_per_sec;
        for (int i = 0; i < interarrival_ns->size(); ++i) {
          (*interarrival_ns)[i] = v;
        }
        break;
      }
      case proto::DIST_UNIFORM: {
        double mean_ns = 1e9 / rpcs_per_sec;
        for (int i = 0; i < interarrival_ns->size(); ++i) {
          (*interarrival_ns)[i] = absl::Uniform(rng, 0, 2 * mean_ns);
        }
        break;
      }
      case proto::DIST_EXPONENTIAL: {
        for (int i = 0; i < interarrival_ns->size(); ++i) {
          (*interarrival_ns)[i] = 1e9 * absl::Exponential(rng, rpcs_per_sec);
          ;
        }
        break;
      }
      default:
        SHARD_LOG(FATAL) << "unsupported interarrival distribution: "
                         << p.interarrival_dist() WITH_NEWLINE;
    }

    const double mean_interarrival_ns = MeanOf(*interarrival_ns);
    CHECK_GT(mean_interarrival_ns, 0.95 * 1e9 / rpcs_per_sec);
    CHECK_LT(mean_interarrival_ns, 1.05 * 1e9 / rpcs_per_sec);

    absl::Duration run_dur;
    if (!absl::ParseDuration(p.run_dur(), &run_dur)) {
      SHARD_LOG(FATAL) << "invalid run duration: " << p.run_dur() WITH_NEWLINE;
    }

    hr_cum_run_dur += absl::ToInt64Nanoseconds(run_dur);

    stages.push_back({
        .rpc_size_bytes = p.rpc_size_bytes(),
        .target_average_bps = p.target_average_bps(),
        .interarrival_ns = std::move(interarrival_ns),
        .hr_cum_run_dur = hr_cum_run_dur,
    });
  }
  return stages;
}

int32_t GetMaxRpcSizeBytes(const std::vector<WorkloadStage>& workload_stages) {
  int32_t max_size = -1;
  for (const WorkloadStage& s : workload_stages) {
    max_size = std::max(max_size, s.rpc_size_bytes);
  }
  return max_size;
}

int32_t GetMinRpcSizeBytes(const std::vector<WorkloadStage>& workload_stages) {
  int32_t min_size = std::numeric_limits<int32_t>::max();
  for (const WorkloadStage& s : workload_stages) {
    min_size = std::min(min_size, s.rpc_size_bytes);
  }
  return min_size;
}

class InterarrivalRecorder {
 public:
  explicit InterarrivalRecorder(bool should_record)
      : hist_(InterarrivalConfig()),
        should_record_(should_record),
        record_called_(false),
        hr_last_recorded_time_(0) {}

  void RecordIssuedNow() {
    if (!should_record_) {
      return;
    }
    uint64_t now = uv_hrtime();
    if (!record_called_) {
      hr_last_recorded_time_ = now;
      record_called_ = true;
      return;
    }
    hist_.RecordValue(now - hr_last_recorded_time_);
    hr_last_recorded_time_ = now;
  }

  bool is_recording() const { return should_record_; }
  const HdrHistogram& hist() const { return hist_; }

 private:
  HdrHistogram hist_;
  bool should_record_;
  bool record_called_;
  uint64_t hr_last_recorded_time_;
};

class WorkloadController {
 public:
  explicit WorkloadController(const proto::TestLopriClientConfig& c,
                              uint64_t hr_start_time, bool rec_interarrival,
                              StatsRecorder* stats_recorder, ClientConnPool* pool)
      : workload_stages_(StagesFromConfig(c)),
        hr_start_time_(hr_start_time),
        hr_end_time_(hr_start_time + workload_stages_.back().hr_cum_run_dur),
        interarrival_recorder_(rec_interarrival),
        stats_recorder_(stats_recorder),
        conn_pool_(pool),
        hr_report_dur_(1'000'000'000),
        hr_next_report_time_(hr_start_time_ + hr_report_dur_),
        hr_next_i_(0),
        cur_stage_index_(0),
        rpc_id_(0) {}

  uint64_t NextDelay() {
    const WorkloadStage* stage = cur_stage();
    if (stage == nullptr) {
      return std::numeric_limits<uint64_t>::max();
    }
    uint64_t delay =
        stage->interarrival_ns->at(hr_next_i_ % stage->interarrival_ns->size());
    ++hr_next_i_;
    return delay;
  }

  enum ReqStatus {
    kOk,
    kTearingDown,
  };

  // TryIssue will try to issue the request.
  //
  // hr_now should be the time at which the request should be scheduled (e.g. "now" in a
  // closed or semi-closed workload, or the intended request time for an open workload).
  //
  // The request may only fail if all workload stages are complete.
  ReqStatus TryIssueRequest(uint64_t hr_now) {
    const WorkloadStage* stage = cur_stage();
    if (stage != nullptr && hr_now > hr_start_time_ + stage->hr_cum_run_dur) {
      SHARD_LOG(INFO) << "finished workload stage = " << cur_stage_index_++ WITH_NEWLINE;
    }

    if (stage == nullptr || hr_now > hr_end_time_) {
      SHARD_LOG(INFO) << "exiting event loop after "
                      << static_cast<double>(hr_now - hr_start_time_) / 1e9
                      << " sec" WITH_NEWLINE;
      auto st = stats_recorder_->Close();
      if (!st.ok()) {
        SHARD_LOG(FATAL) << "error while recording: " << st WITH_NEWLINE;
      }
      return kTearingDown;
    } else if (hr_now > hr_next_report_time_) {
      int step = (hr_next_report_time_ - hr_start_time_) / hr_report_dur_;
      SHARD_LOG(INFO) << "gathering stats for step " << step WITH_NEWLINE;
      stats_recorder_->DoneStep(absl::StrCat("step=", step));
      hr_next_report_time_ += hr_report_dur_;
    }

    // Issue the request
    uint64_t rpc_id = ++rpc_id_;
    conn_pool_->WithConn([stage, hr_now, rpc_id, this](ClientConn* conn) {
      conn->AssertValid();
      conn->IssueRpc(rpc_id, hr_now /* scheduled time */, stage->rpc_size_bytes);
      interarrival_recorder_.RecordIssuedNow();
    });

    return kOk;
  }

  proto::HdrHistogram GetInterarrivalProto() {
    CHECK(interarrival_recorder_.is_recording());
    return interarrival_recorder_.hist().ToProto();
  }

 private:
  const WorkloadStage* cur_stage() const {
    if (cur_stage_index_ < workload_stages_.size()) {
      return &workload_stages_[cur_stage_index_];
    }
    return nullptr;
  }

  const std::vector<WorkloadStage> workload_stages_;
  const uint64_t hr_start_time_;
  const uint64_t hr_end_time_;
  InterarrivalRecorder interarrival_recorder_;
  StatsRecorder* stats_recorder_;
  ClientConnPool* conn_pool_;
  const uint64_t hr_report_dur_;
  uint64_t hr_next_report_time_;
  size_t hr_next_i_;
  int cur_stage_index_;
  uint64_t rpc_id_;
};

// DeadlineExpired checks if hr_now >= hr_deadline while accounting for precision of the
// timer clock. This means that sometimes DeadlineExpired will return true if hr_now is
// close to hr_deadline, even if hr_deadline hasn't quite arrived yet.
bool DeadlineExpired(uint64_t hr_now, uint64_t hr_deadline) {
  constexpr uint64_t kTimerPrecision = 10'000;  // 10 us
  return hr_deadline <= (hr_now + kTimerPrecision / 2);
}

class LoadGenerator {
 public:
  virtual ~LoadGenerator() = default;
  virtual void RunLoop() = 0;
  virtual proto::HdrHistogram GetInterarrivalProto() = 0;
};

// OpenLoadGenerator generates a schedule of requests (and issues them) independently of
// how quickly the server is able to respond.
//
// Issuing requests is subject to delays on the machine (e.g. CPU starvation, blocked on
// TCP acks), but these delays should be captured in the latency results.
class OpenLoadGenerator : public LoadGenerator {
 public:
  OpenLoadGenerator(const proto::TestLopriClientConfig& c, uv_loop_t* l,
                    std::unique_ptr<StatsRecorder> srec, bool rec_interarrival,
                    std::vector<std::pair<uint64_t, uint64_t>>* goodput_ts,
                    std::vector<struct sockaddr_in> dst_addrs, uint64_t hr_start_time)
      : stats_recorder_(std::move(srec)),
        loop_(l),
        issued_first_req_(false),
        hr_start_time_(hr_start_time),
        tearing_down_(false),
        conn_pool_(
            ClientConnOptions{
                .loop = loop_,
                .stats_recorder = stats_recorder_.get(),
                .goodput_ts = goodput_ts,
                .max_rpc_size_bytes = GetMaxRpcSizeBytes(StagesFromConfig(c)),
                .hr_connect_deadline = hr_start_time_,
                .on_pool_init =
                    absl::bind_front(&OpenLoadGenerator::StartRequestLoop, this),
            },
            c.num_conns(), std::move(dst_addrs)),
        workload_controller_(c, hr_start_time, rec_interarrival, stats_recorder_.get(),
                             &conn_pool_) {}

  void RunLoop() override { uv_run(loop_, UV_RUN_DEFAULT); }

  proto::HdrHistogram GetInterarrivalProto() override {
    return workload_controller_.GetInterarrivalProto();
  }

 private:
  void UpdateNextSendTimeHighRes() {
    uint64_t delay = workload_controller_.NextDelay();
    if (delay == std::numeric_limits<uint64_t>::max()) {
      hr_next_ = delay;
    } else {
      hr_next_ += delay;
    }
    return;
  }

  bool TryIssueRequest(uint64_t hr_now) {
    if (!DeadlineExpired(hr_now, hr_next_)) {
      return false;  // not yet
    }

    switch (workload_controller_.TryIssueRequest(hr_next_ /* use scheduled time */)) {
      case WorkloadController::kOk:
        UpdateNextSendTimeHighRes();
        return true;
      case WorkloadController::kTearingDown:
        tearing_down_ = true;
        uv_stop(loop_);
        return false;
    }

    SHARD_LOG(FATAL) << "unknown status from workload controller" WITH_NEWLINE;
    return false;
  }

  void StartRequestLoop() {
    auto hr_timeout = UvTimeoutUntil(loop_, hr_start_time_);
    // Add callback to start the requests.
    SHARD_LOG(INFO) << "will wait for " << static_cast<double>(hr_timeout) / 1e3
                    << " seconds to issue requests" WITH_NEWLINE;
    timer_ = absl::make_unique<uv_timer_t>();
    uv_timer_init(loop_, timer_.get());
    timer_->data = this;
    uv_timer_start(
        timer_.get(),
        [](uv_timer_t* t) {
          auto self = reinterpret_cast<OpenLoadGenerator*>(t->data);
          self->OnNextReq();
        },
        hr_timeout, 0);
  }

  // Called on every iteration of the event loop
  void OnCheckNextReq() {
    const uint64_t hr_now = uv_hrtime();
    while (!tearing_down_ && TryIssueRequest(hr_now)) {
      /* issue all requests whose time has passed */
    }
    if (tearing_down_) {
      uv_check_stop(check_.get());
      check_ = nullptr;
    }
  }

  void OnNextReq() {
    const uint64_t hr_now = uv_hrtime();
    if (!issued_first_req_) {
      issued_first_req_ = true;
      SHARD_LOG(INFO) << "starting to issue requests" WITH_NEWLINE;
      stats_recorder_->StartRecording();
      hr_next_ = hr_now;

      check_ = absl::make_unique<uv_check_t>();
      uv_check_init(loop_, check_.get());
      check_->data = this;
      uv_check_start(check_.get(), [](uv_check_t* c) {
        auto self = reinterpret_cast<OpenLoadGenerator*>(c->data);
        self->OnCheckNextReq();
      });
    }

    while (!tearing_down_ && TryIssueRequest(hr_now)) {
      /* issue all requests whose time has passed */
    }

    if (!tearing_down_) {
      // Schedule issuing of next request
      uv_timer_start(
          timer_.get(),
          [](uv_timer_t* t) {
            auto self = reinterpret_cast<OpenLoadGenerator*>(t->data);
            self->OnNextReq();
          },
          UvTimeoutUntil(loop_, hr_next_), 0);
    }
  }

  std::unique_ptr<StatsRecorder> stats_recorder_;
  uv_loop_t* loop_;
  uint64_t hr_next_;
  bool issued_first_req_;

  const uint64_t hr_start_time_;

  bool tearing_down_;

  ClientConnPool conn_pool_;
  WorkloadController workload_controller_;

  std::unique_ptr<uv_timer_t> timer_;
  std::unique_ptr<uv_check_t> check_;
};

// NumWritesIn computes how many rpcs can fit inside the specified queue size.
int NumWritesIn(int64_t rpc_size, int64_t queue_size) {
  int64_t num = queue_size / rpc_size;
  num = std::max(num, int64_t{1});
  return num;
}

// SemiOpenLoadGenerator generates a schedule of requests (and issues them) that is
// somewhat independently of how quickly the server can respond. More precisely, the
// generator will wait for requests to be enqueued locally onto the network socket and
// only then, schedule the following request.
//
// The benefit of this, compared to an OpenLoadGenerator, is that it will respond to
// backpressue and issue fewer requests (immitating a real client that sheds load) and
// provides reasonable latency data for the requests that do make it through.
//
// The downside is that it provides less accurate latency data for systems that are
// not overloaded.
class SemiOpenLoadGenerator : public LoadGenerator {
 public:
  constexpr static int64_t kMaxQueueLenPerConnBytes = 102400;  // 100 KB

  SemiOpenLoadGenerator(const proto::TestLopriClientConfig& c, uv_loop_t* l,
                        std::unique_ptr<StatsRecorder> srec, bool rec_interarrival,
                        std::vector<std::pair<uint64_t, uint64_t>>* goodput_ts,
                        std::vector<struct sockaddr_in> dst_addrs, uint64_t hr_start_time)
      : stats_recorder_(std::move(srec)),
        loop_(l),
        issued_first_req_(false),
        issued_scheduled_req_(false),
        timer_is_running_(false),
        hr_start_time_(hr_start_time),
        tearing_down_(false),
        conn_pool_(
            ClientConnOptions{
                .loop = loop_,
                .stats_recorder = stats_recorder_.get(),
                .goodput_ts = goodput_ts,
                .max_rpc_size_bytes = GetMaxRpcSizeBytes(StagesFromConfig(c)),
                .hr_connect_deadline = hr_start_time_,
                .on_pool_init =
                    absl::bind_front(&SemiOpenLoadGenerator::StartRequestLoop, this),
                .on_write_done =
                    absl::bind_front(&SemiOpenLoadGenerator::OnWriteDone, this),
            },
            c.num_conns(), std::move(dst_addrs)),
        max_queued_writes_(NumWritesIn(GetMinRpcSizeBytes(StagesFromConfig(c)),
                                       kMaxQueueLenPerConnBytes) *
                           c.num_conns()),
        num_wip_write_(0),
        workload_controller_(c, hr_start_time, rec_interarrival, stats_recorder_.get(),
                             &conn_pool_) {}

  void RunLoop() override { uv_run(loop_, UV_RUN_DEFAULT); }

  proto::HdrHistogram GetInterarrivalProto() override {
    return workload_controller_.GetInterarrivalProto();
  }

 private:
  enum class IssueCaller {
    Recheck,  // when MaybeIssueScheduleRequest returns true
    OnWriteDone,
    OnCheckNextReq,
    OnTimerTick,
  };

  static absl::string_view ToString(IssueCaller c) {
    switch (c) {
      case IssueCaller::Recheck:
        return "Recheck";
      case IssueCaller::OnWriteDone:
        return "OnWriteDone";
      case IssueCaller::OnCheckNextReq:
        return "OnCheckNextReq";
      case IssueCaller::OnTimerTick:
        return "OnTimerTick";
      default:
        return "UNKNOWN";
    }
  }

  // Called to issue the first request, on every iteration of the event loop, when writes
  // complete, and when the current timer expires.
  //
  // Returns true if more requests should be attempted
  ABSL_MUST_USE_RESULT bool MaybeIssueScheduleRequest(uint64_t hr_now,
                                                      IssueCaller caller_type) {
    if (tearing_down_) {
      return false;
    }

    bool waiting_on_wip_write = num_wip_write_ == max_queued_writes_;
    if (caller_type == IssueCaller::OnWriteDone) {
      waiting_on_wip_write = num_wip_write_ >= max_queued_writes_ - 1;
    }
    bool waiting_on_timer = timer_is_running_ || caller_type == IssueCaller::OnTimerTick;

    if (!issued_first_req_) {
      issued_first_req_ = true;
      SHARD_LOG(INFO) << "starting to issue requests" WITH_NEWLINE;
      stats_recorder_->StartRecording();
      hr_scheduled_time_ = hr_now;

      check_ = absl::make_unique<uv_check_t>();
      uv_check_init(loop_, check_.get());
      check_->data = this;
      uv_check_start(check_.get(), [](uv_check_t* c) {
        auto self = reinterpret_cast<SemiOpenLoadGenerator*>(c->data);
        self->OnCheckNextReq();
      });
    } else {
      CHECK(waiting_on_timer || waiting_on_wip_write ||
            caller_type == IssueCaller::Recheck)
          << absl::StrFormat(
                 "caller_type = %s, timer_is_running_ = %d, %d/%d queued writes",
                 ToString(caller_type), timer_is_running_, num_wip_write_,
                 max_queued_writes_);
    }

    if (!DeadlineExpired(hr_now, hr_scheduled_time_)) {
      CHECK(waiting_on_timer || waiting_on_wip_write ||
            caller_type == IssueCaller::Recheck)
          << absl::StrFormat(
                 "caller_type = %s, timer_is_running_ = %d, %d/%d queued writes",
                 ToString(caller_type), timer_is_running_, num_wip_write_,
                 max_queued_writes_);
      if (kDebugLoadGen) {
        SHARD_LOG(INFO) << "still need to wait for "
                        << (hr_scheduled_time_ - hr_now) / 1'000
                        << " us num_wip_write_ = " << num_wip_write_;
      }
      if (!timer_is_running_ && num_wip_write_ < max_queued_writes_) {
        // Restart timer
        if (kDebugLoadGen) {
          SHARD_LOG(INFO) << "start timer" WITH_NEWLINE;
        }
        timer_is_running_ = true;
        uv_timer_start(
            timer_.get(),
            [](uv_timer_t* t) {
              auto self = reinterpret_cast<SemiOpenLoadGenerator*>(t->data);
              self->OnTimerTick();
            },
            UvTimeoutUntil(loop_, hr_scheduled_time_), 0);
      }
      return false;
    }

    if (!issued_scheduled_req_) {
      // See if we can issue the request

      // Skip if all conns have pending writes. Once the writes complete, we'll come back
      // here.
      if (num_wip_write_ == max_queued_writes_) {
        if (kDebugLoadGen) {
          SHARD_LOG(INFO) << absl::StrFormat("too busy: %d/%d queued writes",
                                             num_wip_write_, max_queued_writes_)
                  WITH_NEWLINE;
        }
        return false;
      }

      issued_scheduled_req_ = true;
      switch (workload_controller_.TryIssueRequest(hr_scheduled_time_)) {
        case WorkloadController::kOk:
          // continue on to scheduling the next request
          ++num_wip_write_;
          break;
        case WorkloadController::kTearingDown:
          tearing_down_ = true;
          uv_stop(loop_);
          break;
        default:
          SHARD_LOG(FATAL) << "unknown status from workload controller" WITH_NEWLINE;
          return false;
      }
    } else {
      if (kDebugLoadGen) {
        SHARD_LOG(INFO) << "request issued already" WITH_NEWLINE;
      }
    }

    if (timer_is_running_) {
      if (kDebugLoadGen) {
        SHARD_LOG(INFO) << "stop timer" WITH_NEWLINE;
      }
      uv_timer_stop(timer_.get());
      timer_is_running_ = false;
    }

    if (tearing_down_) {
      if (kDebugLoadGen) {
        SHARD_LOG(INFO) << "don't schedule: tearing down" WITH_NEWLINE;
      }
      return false;
    }

    {
      uint64_t base_time = hr_scheduled_time_;
      if (caller_type == IssueCaller::OnWriteDone) {
        base_time = hr_now;
      }

      // Schedule next request
      uint64_t delay = workload_controller_.NextDelay();
      if (delay == std::numeric_limits<uint64_t>::max()) {
        hr_scheduled_time_ = delay;
      } else {
        hr_scheduled_time_ = base_time + delay;
      }
      issued_scheduled_req_ = false;
    }

    return num_wip_write_ < max_queued_writes_;
  }

  void StartRequestLoop() {
    auto hr_timeout = UvTimeoutUntil(loop_, hr_start_time_);
    // Add callback to start the requests.
    SHARD_LOG(INFO) << "will wait for " << static_cast<double>(hr_timeout) / 1e3
                    << " seconds to issue requests" WITH_NEWLINE;
    timer_ = absl::make_unique<uv_timer_t>();
    uv_timer_init(loop_, timer_.get());
    timer_->data = this;
    uv_timer_start(
        timer_.get(),
        [](uv_timer_t* t) {
          auto self = reinterpret_cast<SemiOpenLoadGenerator*>(t->data);
          self->OnTimerTick();
        },
        hr_timeout, 0);
  }

  void OnWriteDone() {
    if (kDebugLoadGen) {
      SHARD_LOG(INFO) << "write is done" WITH_NEWLINE;
    }
    --num_wip_write_;
    uint64_t hr_now = uv_hrtime();
    if (MaybeIssueScheduleRequest(hr_now, IssueCaller::OnWriteDone)) {
      while (MaybeIssueScheduleRequest(hr_now, IssueCaller::Recheck)) {
        // Issue additional due requests
      }
    }
  }

  // Called on every iteration of the event loop
  void OnCheckNextReq() {
    if (kDebugLoadGen) {
      SHARD_LOG(INFO) << "check" WITH_NEWLINE;
    }
    uint64_t hr_now = uv_hrtime();
    if (MaybeIssueScheduleRequest(hr_now, IssueCaller::OnCheckNextReq)) {
      while (MaybeIssueScheduleRequest(hr_now, IssueCaller::Recheck)) {
        // Issue additional due requests
      }
    }
    if (tearing_down_) {
      uv_check_stop(check_.get());
      check_ = nullptr;
    }
  }

  void OnTimerTick() {
    if (kDebugLoadGen) {
      SHARD_LOG(INFO) << "timer tick" WITH_NEWLINE;
    }
    timer_is_running_ = false;
    uint64_t hr_now = uv_hrtime();
    if (MaybeIssueScheduleRequest(hr_now, IssueCaller::OnTimerTick)) {
      while (MaybeIssueScheduleRequest(hr_now, IssueCaller::Recheck)) {
        // Issue additional due requests
      }
    }
  }

  std::unique_ptr<StatsRecorder> stats_recorder_;
  uv_loop_t* loop_;
  uint64_t hr_scheduled_time_;
  bool issued_first_req_;
  bool issued_scheduled_req_;
  bool timer_is_running_;

  const uint64_t hr_start_time_;

  bool tearing_down_;

  ClientConnPool conn_pool_;
  const int max_queued_writes_;
  int num_wip_write_;
  WorkloadController workload_controller_;

  std::unique_ptr<uv_timer_t> timer_;
  std::unique_ptr<uv_check_t> check_;
};

absl::StatusOr<std::unique_ptr<LoadGenerator>> MakeLoadGenerator(
    const proto::TestLopriClientConfig& c, uv_loop_t* l,
    std::unique_ptr<StatsRecorder> srec, bool rec_interarrival,
    std::vector<std::pair<uint64_t, uint64_t>>* goodput_ts,
    std::vector<struct sockaddr_in> dst_addrs, uint64_t hr_start_time) {
  switch (c.load_gen()) {
    case proto::LG_OPEN:
      return absl::make_unique<OpenLoadGenerator>(c, l, std::move(srec), rec_interarrival,
                                                  goodput_ts, std::move(dst_addrs),
                                                  hr_start_time);
    case proto::LG_SEMI_OPEN:
      return absl::make_unique<SemiOpenLoadGenerator>(
          c, l, std::move(srec), rec_interarrival, goodput_ts, std::move(dst_addrs),
          hr_start_time);
    default:
      return absl::InvalidArgumentError("unset or unknown load generator");
  }
}

void ParseDestAddrs(absl::string_view server_addrs,
                    std::vector<struct sockaddr_in>* dst_addrs) {
  std::vector<std::string> addrs_to_print;
  for (absl::string_view addr_full : absl::StrSplit(server_addrs, ",")) {
    absl::string_view addr_view;
    int32_t port;
    auto st = ParseHostPort(addr_full, &addr_view, &port);
    if (!st.ok()) {
      SHARD_LOG(FATAL) << "invalid host/port: " << addr_full << ": " << st WITH_NEWLINE;
    }
    std::string addr(addr_view);
    dst_addrs->push_back({});
    uv_ip4_addr(addr.c_str(), port, &dst_addrs->back());
    addrs_to_print.push_back(IP4Name(&dst_addrs->back()));
  }

  SHARD_LOG(INFO) << "will connect to addresses: "
                  << absl::StrJoin(addrs_to_print, ", ") WITH_NEWLINE;
}

absl::Status WriteGoodputTS(
    const std::vector<std::pair<uint64_t, uint64_t>>& per_ms_goodput,
    const std::string& path) {
  FILE* f = fopen(path.c_str(), "w");
  if (f == nullptr) {
    return absl::UnknownError(
        absl::StrFormat("failed to open file '%s': %s\n", path, heyp::StrError(errno)));
  }
  for (const auto& time_gp_pair : per_ms_goodput) {
    absl::FPrintF(f, "%d,%d\n", time_gp_pair.first, time_gp_pair.second);
  }
  if (fclose(f) != 0) {
    return absl::UnknownError("failed to write data");
  }
  return absl::OkStatus();
}

}  // namespace
}  // namespace heyp

DEFINE_string(c, "config.textproto", "path to input config");
DEFINE_string(server, "127.0.0.1:7777", "comma-separated addresses of servers");
DEFINE_string(out, "testlopri-client.log", "path to log output");
DEFINE_string(start_time, "", "wait until this time to start the run");
DEFINE_string(interarrival, "",
              "path to write out interarrival distribution (optional, for validation)");
DEFINE_string(msgput, "",
              "path to write out per-ms goodput distribution (optional, for validation)");

int ShardMain(int argc, char** argv, int shard_index, int num_shards) {
  heyp::ThisShardIndex = shard_index;
  heyp::MainInit(&argc, &argv);
  auto srec =
      heyp::StatsRecorder::Create(absl::StrCat(FLAGS_out, ".shard.", shard_index));
  if (!srec.ok()) {
    std::cerr << "failed to create stats recorder: " << srec.status();
    return 2;
  }

  absl::Time start_time = absl::Now() + absl::Seconds(3);
  if (!FLAGS_start_time.empty()) {
    std::string err;
    if (!absl::ParseTime(absl::RFC3339_full, FLAGS_start_time, &start_time, &err)) {
      std::cerr << "invalid start time: " << err << "\n";
      return 3;
    }
  }

  if (shard_index == 0) {
    std::cout << absl::StrFormat(
        "start-time: %s start-time-unix-ms: %d\n",
        absl::FormatTime(absl::RFC3339_full, start_time, absl::UTCTimeZone()),
        absl::ToUnixMillis(start_time));
  }

  heyp::proto::TestLopriClientConfig config;
  if (!heyp::ReadTextProtoFromFile(FLAGS_c, &config)) {
    std::cerr << "failed to parse config file\n";
    return 4;
  }

  for (int i = 0; i < config.workload_stages_size(); ++i) {
    config.mutable_workload_stages(i)->set_target_average_bps(
        config.workload_stages(i).target_average_bps() / num_shards);
  }

  int64_t ns_until_start = absl::ToInt64Nanoseconds(start_time - absl::Now());
  uint64_t hr_start_time = 0;
  if (ns_until_start < 0) {
    hr_start_time = uv_hrtime();
  } else {
    hr_start_time = uv_hrtime() + ns_until_start;
  }

  std::unique_ptr<std::vector<std::pair<uint64_t, uint64_t>>> goodput_ts;
  if (FLAGS_msgput != "") {
    goodput_ts = absl::make_unique<std::vector<std::pair<uint64_t, uint64_t>>>();
  }

  std::vector<struct sockaddr_in> dst_addrs;
  heyp::ParseDestAddrs(FLAGS_server, &dst_addrs);
  std::unique_ptr<heyp::LoadGenerator> load_gen;
  {
    auto load_gen_or = heyp::MakeLoadGenerator(
        config, uv_default_loop(), std::move(*srec), FLAGS_interarrival != "",
        goodput_ts.get(), std::move(dst_addrs), hr_start_time);
    if (!load_gen_or.ok()) {
      std::cerr << load_gen_or.status() << "\n";
      return 5;
    }
    load_gen = std::move(*load_gen_or);
  }
  load_gen->RunLoop();

  absl::Time end_time = absl::Now();
  std::cout << absl::StrFormat(
      "end-time: %s end-time-unix-ms: %d\n",
      absl::FormatTime(absl::RFC3339_full, end_time, absl::UTCTimeZone()),
      absl::ToUnixMillis(end_time));

  if (FLAGS_interarrival != "") {
    CHECK(heyp::WriteTextProtoToFile(
        load_gen->GetInterarrivalProto(),
        absl::StrCat(FLAGS_interarrival, ".shard.", shard_index)))
        << "failed to write latency hist";
  }
  if (FLAGS_msgput != "") {
    absl::Status st = heyp::WriteGoodputTS(
        *goodput_ts, absl::StrCat(FLAGS_msgput, ".shard.", shard_index));
    if (!st.ok()) {
      LOG(FATAL) << "failed to write goodput timeseries: " << st;
    }
  }
  return 0;
}

int main(int argc, char** argv) {
  int num_shards = 1;
  bool next_is_num_shards = false;
  for (int i = 1; i < argc;) {
    if (strcmp(argv[i], "-shards") == 0) {
      next_is_num_shards = true;
      memmove(argv + i, argv + i + 1, sizeof(char*) * argc - i);
      --argc;
      continue;
    }
    if (absl::StartsWith(argv[i], "-shards=")) {
      if (!absl::SimpleAtoi(absl::StripPrefix(argv[i], "-shards="), &num_shards)) {
        std::cerr << "failed to parse -shards value\n";
        return 9;
      }
      memmove(argv + i, argv + i + 1, sizeof(char*) * argc - i);
      --argc;
      continue;
    }
    if (next_is_num_shards) {
      if (!absl::SimpleAtoi(argv[i], &num_shards)) {
        std::cerr << "failed to parse -shards value\n";
        return 9;
      }
      memmove(argv + i, argv + i + 1, sizeof(char*) * argc - i);
      --argc;
      next_is_num_shards = false;
      continue;
    }

    ++i;
  }

  std::string args;
  for (int i = 0; i < argc; ++i) {
    args += std::string(argv[i]);
    args += " ";
  }

  if (next_is_num_shards) {
    std::cerr << "-shards is missing value\n";
    return 9;
  }
  if (num_shards == 0) {
    std::cerr << "-shards must be > 0\n";
    return 0;
  }

  if (num_shards == 1) {
    return ShardMain(argc, argv, 0, 1);
  }

  pid_t* pids = new pid_t[num_shards];
  for (int i = 0; i < num_shards; ++i) {
    pid_t pid = fork();
    if (pid == 0) {
      return ShardMain(argc, argv, i, num_shards);
    }
    pids[i] = pid;
  }

  int ret = 0;
  for (int i = 0; i < num_shards; ++i) {
    int status = 0;
    waitpid(pids[i], &status, 0);
    ret = std::max(ret, status);
  }
  return ret;
}
