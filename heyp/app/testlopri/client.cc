#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <uv.h>

#include <array>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

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

constexpr int kNumParallelRpcsPerConn = 32;
constexpr bool kDebugReadWrite = false;
constexpr bool kLimitedDebugReadWrite = false;
constexpr bool kDebugPool = false;

int ThisShardIndex = 0;  // Initialized by ShardMain

bool InLimitedDebugReadWrite() {
  static int32_t LimitedDebugReadWriteCounter = 0;
  return kLimitedDebugReadWrite && LimitedDebugReadWriteCounter++ <= 300;
}

class CachedTime {
 public:
  uint64_t Get() {
    if (!did_get_) {
      now_ = uv_hrtime();
      did_get_ = true;
    }
    return now_;
  }

 private:
  uint64_t now_ = 0;
  bool did_get_ = false;
};

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

class ClientConnPool {
 public:
  ClientConnPool(int num_conns, int32_t max_rpc_size_bytes,
                 std::vector<struct sockaddr_in> dst_addrs, uint64_t hr_start_time,
                 const std::function<void()>& on_pool_ready, uv_loop_t* loop,
                 StatsRecorder* stats_recorder, bool* tearing_down);

  bool AddConn(ClientConn* conn);
  void WithConn(const std::function<void(ClientConn*)>& func);

 private:
  const int num_conns_;
  const std::vector<struct sockaddr_in> dst_addrs_;

  std::deque<ClientConn*> ready_;
  std::deque<std::function<void(ClientConn*)>> to_exec_;
  std::vector<std::unique_ptr<ClientConn>> all_;

  bool* tearing_down_;
};

class ClientConn {
 public:
  ClientConn(ClientConnPool* pool, int32_t max_rpc_size_bytes, int conn_id,
             const struct sockaddr_in* addr, const std::function<void()>& on_pool_ready,
             uv_loop_t* loop, StatsRecorder* stats_recorder, uint64_t hr_start_run_time)
      : pool_(pool),
        on_pool_ready_(on_pool_ready),
        conn_id_(conn_id),
        addr_(addr),
        hr_start_run_time_(hr_start_run_time),
        loop_(loop),
        stats_recorder_(stats_recorder),
        buf_(max_rpc_size_bytes, 0),
        num_seen_(0),
        next_pos_(0),
        read_buf_size_(0),
        in_pool_(false) {
    CHECK_GE(max_rpc_size_bytes, 12);

    uv_tcp_init(loop_, &client_);
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

  void IssueRpc(uint64_t rpc_id, uint64_t hr_now, int32_t rpc_size_bytes) {
    hr_start_time_[next_pos_ % kNumParallelRpcsPerConn] = hr_now;
    rpc_id_[next_pos_ % kNumParallelRpcsPerConn] = rpc_id;
    ++next_pos_;
    WriteU32LE(rpc_size_bytes - 12, buf_.data());
    WriteU64LE(rpc_id, buf_.data() + 4);
    if (kDebugReadWrite || InLimitedDebugReadWrite()) {
      SHARD_LOG(INFO) << "write(c=" << conn_id_ << ") rpc id=" << rpc_id
                      << " header=" << ToHex(buf_.data(), 12) WITH_NEWLINE;
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

  void AssertValid() const {
    CHECK_LE(num_seen_, next_pos_);
    CHECK_LT(next_pos_, num_seen_ + kNumParallelRpcsPerConn);
  }

 private:
  void OnConnect(uv_connect_t* req, int status) {
    if (uv_hrtime() > hr_start_run_time_) {
      SHARD_LOG(FATAL) << "took too long to connect" WITH_NEWLINE;
    }

    AssertValid();

    if (status < 0) {
      SHARD_LOG(ERROR) << "failed to connect (" << uv_strerror(status)
                       << "); trying again" WITH_NEWLINE;
      uv_tcp_init(loop_, &client_);
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

    if (pool_->AddConn(this)) {
      if (kDebugPool) {
        SHARD_LOG(INFO) << "connection pool is fully initialized" WITH_NEWLINE;
      }
      on_pool_ready_();
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
    auto record_rpc_latency = [&](uint64_t rpc_id) {
      CHECK_EQ(rpc_id_[num_seen_ % kNumParallelRpcsPerConn], rpc_id)
          << "rpcs on a single connection were reordered!";
      stats_recorder_->RecordRpc(
          buf->len,
          absl::Nanoseconds(uv_hrtime() -
                            hr_start_time_[num_seen_ % kNumParallelRpcsPerConn]));
      ++num_seen_;
      got_ack = true;
    };

    char* b = buf->base;
    char* e = buf->base + nread;
    while (b < e) {
      int tocopy = std::min<int>(e - b, 8 - read_buf_size_);
      memmove(read_buf_ + read_buf_size_, b, tocopy);
      b += tocopy;
      read_buf_size_ += tocopy;

      if (read_buf_size_ == 8) {
        if (kDebugReadWrite || InLimitedDebugReadWrite()) {
          SHARD_LOG(INFO) << "read (c=" << conn_id_ << ") rpc id=" << ReadU64LE(read_buf_)
                          << " header=" << ToHex(read_buf_, 8) WITH_NEWLINE;
        }
        record_rpc_latency(ReadU64LE(read_buf_));
        read_buf_size_ = 0;
      }
    }
    delete buf->base;
    if (got_ack && !has_pending_write_) {
      pool_->AddConn(this);  // will not add if present already
    }
  }

  void OnWriteDone(uv_write_t* req, int status) {
    AssertValid();
    if (status < 0) {
      absl::FPrintF(stderr, "write error %s\n", uv_strerror(status));
      --next_pos_;
      delete req;
      return;
    }

    if (kDebugReadWrite || InLimitedDebugReadWrite()) {
      uint64_t rpc_id = reinterpret_cast<uintptr_t>(req->data);
      SHARD_LOG(INFO) << "write(c=" << conn_id_ << ") rpc id=" << rpc_id
                      << " done" WITH_NEWLINE;
    }

    has_pending_write_ = false;

    if (next_pos_ - num_seen_ < kNumParallelRpcsPerConn - 1) {
      pool_->AddConn(this);  // else OnRead will add it back
    }
    delete req;
  }

  ClientConnPool* pool_;
  std::function<void()> on_pool_ready_;

  const int conn_id_;
  const struct sockaddr_in* addr_;
  const uint64_t hr_start_run_time_;

  uv_loop_t* loop_;
  StatsRecorder* stats_recorder_;

  uv_tcp_t client_;
  uv_buf_t buffer_;
  std::string buf_;
  std::array<uint64_t, kNumParallelRpcsPerConn> hr_start_time_;
  std::array<uint64_t, kNumParallelRpcsPerConn> rpc_id_;
  int num_seen_;
  int next_pos_;

  char read_buf_[8];
  int read_buf_size_;

  bool in_pool_;
  bool has_pending_write_ = false;

  friend class ClientConnPool;
};

ClientConnPool::ClientConnPool(int num_conns, int32_t max_rpc_size_bytes,
                               std::vector<struct sockaddr_in> dst_addrs,
                               uint64_t hr_start_time,
                               const std::function<void()>& on_pool_ready,
                               uv_loop_t* loop, StatsRecorder* stats_recorder,
                               bool* tearing_down)
    : num_conns_(num_conns),
      dst_addrs_(std::move(dst_addrs)),
      tearing_down_(tearing_down) {
  // Start by creating connections.
  // Once all are created, the last will register a timer to start the run
  if (kDebugPool) {
    SHARD_LOG(INFO) << "Starting " << num_conns_ << " connections" WITH_NEWLINE;
  }
  for (int i = 0; i < num_conns_; ++i) {
    all_.push_back(absl::make_unique<ClientConn>(
        this, max_rpc_size_bytes, all_.size(), &dst_addrs_.at(i % dst_addrs_.size()),
        on_pool_ready, loop, stats_recorder, hr_start_time));
  }
}

bool ClientConnPool::AddConn(ClientConn* conn) {
  conn->AssertValid();
  if (*tearing_down_) {
    return false;
  }

  if (conn->in_pool_) {
    return ready_.size() == num_conns_;
  }

  if (!to_exec_.empty()) {
    to_exec_.front()(conn);
    to_exec_.pop_front();
    return false;
  }

  ready_.push_back(conn);
  conn->in_pool_ = true;
  return ready_.size() == num_conns_;
}

void ClientConnPool::WithConn(const std::function<void(ClientConn*)>& func) {
  if (!ready_.empty()) {
    ClientConn* conn = ready_.front();
    conn->AssertValid();
    ready_.pop_front();
    conn->in_pool_ = false;

    func(conn);
    return;
  }

  to_exec_.push_back(func);
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

class LoadGenerator {
 public:
  LoadGenerator(const proto::TestLopriClientConfig& c, uv_loop_t* l,
                std::unique_ptr<StatsRecorder> srec, bool rec_interarrival,
                std::vector<struct sockaddr_in> dst_addrs, uint64_t hr_start_time)
      : workload_stages_(StagesFromConfig(c)),
        stats_recorder_(std::move(srec)),
        hr_total_run_dur_(workload_stages_.back().hr_cum_run_dur),
        loop_(l),
        issued_first_req_(false),
        hr_start_time_(hr_start_time),
        hr_report_dur_(1'000'000'000),
        hr_next_report_time_(hr_start_time_ + hr_report_dur_),
        record_interarrival_called_(false),
        tearing_down_(false),
        conn_pool_(c.num_conns(), GetMaxRpcSizeBytes(workload_stages_),
                   std::move(dst_addrs), hr_start_time_,
                   absl::bind_front(&LoadGenerator::StartRequestLoop, this), loop_,
                   stats_recorder_.get(), &tearing_down_) {
    if (rec_interarrival) {
      interarrival_hist_.emplace(InterarrivalConfig());
    }
  }

  void RunLoop() { uv_run(loop_, UV_RUN_DEFAULT); }

  proto::HdrHistogram GetInterarrivalProto() {
    CHECK(interarrival_hist_.has_value());
    return interarrival_hist_->ToProto();
  }

 private:
  void UpdateNextSendTimeHighRes() {
    const WorkloadStage* stage = cur_stage();
    if (stage == nullptr) {
      hr_next_ = std::numeric_limits<uint64_t>::max();
    }

    hr_next_ += stage->interarrival_ns->at(hr_next_i_ % stage->interarrival_ns->size());
    ++hr_next_i_;

    return;
  }

  void RecordInterarrivalIssuedNow() {
    if (!interarrival_hist_.has_value()) {
      return;
    }
    uint64_t now = uv_hrtime();
    if (!record_interarrival_called_) {
      hr_last_recorded_time_ = now;
      record_interarrival_called_ = true;
      return;
    }
    interarrival_hist_->RecordValue(now - hr_last_recorded_time_);
    hr_last_recorded_time_ = now;
  }

  bool TryIssueRequest(CachedTime* time) {
    uint64_t hr_now = time->Get();
    if (hr_now < hr_next_) {
      return false;  // not yet
    }

    if (cur_stage() != nullptr && hr_now > hr_start_time_ + cur_stage()->hr_cum_run_dur) {
      SHARD_LOG(INFO) << "finished workload stage = " << cur_stage_index_++ WITH_NEWLINE;
    }

    if (cur_stage() == nullptr || hr_now > hr_start_time_ + hr_total_run_dur_) {
      SHARD_LOG(INFO) << "exiting event loop after "
                      << static_cast<double>(hr_now - hr_start_time_) / 1e9
                      << " sec" WITH_NEWLINE;
      tearing_down_ = true;
      uv_stop(loop_);
      auto st = stats_recorder_->Close();
      if (!st.ok()) {
        SHARD_LOG(FATAL) << "error while recording: " << st WITH_NEWLINE;
      }
      return false;
    } else if (hr_now > hr_next_report_time_) {
      int step = (hr_next_report_time_ - hr_start_time_) / hr_report_dur_;
      SHARD_LOG(INFO) << "gathering stats for step " << step WITH_NEWLINE;
      stats_recorder_->DoneStep(absl::StrCat("step=", step));
      hr_next_report_time_ += hr_report_dur_;
    }

    // Issue the request
    uint64_t rpc_id = ++rpc_id_;
    conn_pool_.WithConn([hr_now, rpc_id, this](ClientConn* conn) {
      conn->AssertValid();
      const WorkloadStage* stage = cur_stage();
      if (stage == nullptr) {
        conn_pool_.AddConn(conn);
        return;
      }
      conn->IssueRpc(rpc_id, hr_now, stage->rpc_size_bytes);
      RecordInterarrivalIssuedNow();
    });

    UpdateNextSendTimeHighRes();
    return true;
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
          auto self = reinterpret_cast<LoadGenerator*>(t->data);
          self->OnNextReq();
        },
        hr_timeout, 0);
  }

  // Called on every iteration of the event loop
  void OnCheckNextReq() {
    CachedTime time;
    while (!tearing_down_ && TryIssueRequest(&time)) {
      /* issue all requests whose time has passed */
    }
    if (tearing_down_) {
      uv_check_stop(check_.get());
      check_ = nullptr;
    }
  }

  void OnNextReq() {
    CachedTime time;
    if (!issued_first_req_) {
      issued_first_req_ = true;
      SHARD_LOG(INFO) << "starting to issue requests" WITH_NEWLINE;
      stats_recorder_->StartRecording();
      hr_next_ = time.Get();

      check_ = absl::make_unique<uv_check_t>();
      uv_check_init(loop_, check_.get());
      check_->data = this;
      uv_check_start(check_.get(), [](uv_check_t* c) {
        auto self = reinterpret_cast<LoadGenerator*>(c->data);
        self->OnCheckNextReq();
      });
    }

    while (!tearing_down_ && TryIssueRequest(&time)) {
      /* issue all requests whose time has passed */
    }

    if (!tearing_down_) {
      // Schedule issuing of next request
      uv_timer_start(
          timer_.get(),
          [](uv_timer_t* t) {
            auto self = reinterpret_cast<LoadGenerator*>(t->data);
            self->OnNextReq();
          },
          UvTimeoutUntil(loop_, hr_next_), 0);
    }
  }

  const WorkloadStage* cur_stage() const {
    if (cur_stage_index_ < workload_stages_.size()) {
      return &workload_stages_[cur_stage_index_];
    }
    return nullptr;
  }

  const std::vector<WorkloadStage> workload_stages_;
  int cur_stage_index_ = 0;
  std::unique_ptr<StatsRecorder> stats_recorder_;
  int64_t hr_total_run_dur_;
  uv_loop_t* loop_;
  uint64_t hr_next_;
  size_t hr_next_i_ = 0;
  bool issued_first_req_;

  const uint64_t hr_start_time_;
  const uint64_t hr_report_dur_;
  uint64_t hr_next_report_time_;
  uint64_t rpc_id_ = 0;

  uint64_t hr_last_recorded_time_;
  absl::optional<HdrHistogram> interarrival_hist_;
  bool record_interarrival_called_;

  bool tearing_down_;

  ClientConnPool conn_pool_;

  std::unique_ptr<uv_timer_t> timer_;
  std::unique_ptr<uv_check_t> check_;
};

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

}  // namespace
}  // namespace heyp

DEFINE_string(c, "config.textproto", "path to input config");
DEFINE_string(server, "127.0.0.1:7777", "comma-separated addresses of servers");
DEFINE_string(out, "testlopri-client.log", "path to log output");
DEFINE_string(start_time, "", "wait until this time to start the run");
DEFINE_string(interarrival, "",
              "path to write out interarrival distribution (optional, for validation)");

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

  std::vector<struct sockaddr_in> dst_addrs;
  heyp::ParseDestAddrs(FLAGS_server, &dst_addrs);
  heyp::LoadGenerator load_gen(config, uv_default_loop(), std::move(*srec),
                               FLAGS_interarrival != "", std::move(dst_addrs),
                               hr_start_time);
  load_gen.RunLoop();

  absl::Time end_time = absl::Now();
  std::cout << absl::StrFormat(
      "end-time: %s end-time-unix-ms: %d\n",
      absl::FormatTime(absl::RFC3339_full, end_time, absl::UTCTimeZone()),
      absl::ToUnixMillis(end_time));

  if (FLAGS_interarrival != "") {
    CHECK(heyp::WriteTextProtoToFile(
        load_gen.GetInterarrivalProto(),
        absl::StrCat(FLAGS_interarrival, ".shard.", shard_index)))
        << "failed to write latency hist";
  }
  return 0;
}

int main(int argc, char** argv) {
  int num_shards = 1;
  bool next_is_num_shards = false;
  for (int i = 1; i < argc;) {
    if (strcmp(argv[i], "-shards") == 0) {
      next_is_num_shards = true;
      --argc;
      memmove(argv + i, argv + i + 1, argc - i);
      continue;
    }
    if (absl::StartsWith(argv[i], "-shards=")) {
      if (!absl::SimpleAtoi(absl::StripPrefix(argv[i], "-shards="), &num_shards)) {
        std::cerr << "failed to parse -shards value\n";
        return 9;
      }
      --argc;
      memmove(argv + i, argv + i + 1, argc - i);
      continue;
    }
    if (next_is_num_shards) {
      if (!absl::SimpleAtoi(argv[i], &num_shards)) {
        std::cerr << "failed to parse -shards value\n";
        return 9;
      }
      --argc;
      memmove(argv + i, argv + i + 1, argc - i);
      next_is_num_shards = false;
      continue;
    }

    ++i;
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
