#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <uv.h>

#include <deque>
#include <vector>

#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
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

constexpr int kNumParallelRpcsPerConn = 32;
constexpr bool kDebug = false;

typedef struct {
  uv_tcp_t client;
  uv_buf_t buffer;
  char* buf;
  uint64_t hr_start_time[kNumParallelRpcsPerConn];
  uint64_t rpc_id[kNumParallelRpcsPerConn];
  int num_seen;
  int next_pos;

  char read_buf[8];
  int read_buf_size;

  int conn_id;

  bool in_pool;
} client_conn_t;

proto::HdrHistogram::Config InterarrivalConfig() {
  proto::HdrHistogram::Config c;
  c.set_highest_trackable_value(1'000'000'000);
  c.set_lowest_discernible_value(100);
  c.set_significant_figures(2);
  return c;
}

struct State {
  State(const proto::TestLopriClientConfig& c, uv_loop_t* l,
        std::unique_ptr<StatsRecorder> srec, bool rec_interarrival)
      : config(c),
        stats_recorder(std::move(srec)),
        loop(l),
        issued_first_req(false),
        hr_report_dur(1'000'000'000),
        record_interarrival_called(false),
        tearing_down(false) {
    double rpcs_per_sec = config.target_average_bps() / (8 * config.rpc_size_bytes());
    LOG(INFO) << "targeting an average of " << rpcs_per_sec << "rpcs/sec";
    switch (config.interarrival_dist()) {
      case proto::Distribution::CONSTANT:
        dist_param = 1.0 / rpcs_per_sec;
        break;
      case proto::Distribution::UNIFORM:
        dist_param = 1.0 / rpcs_per_sec;
        break;
      case proto::Distribution::EXPONENTIAL:
        dist_param = rpcs_per_sec;
        break;
      default:
        LOG(FATAL) << "unsupported interarrival distribution: "
                   << config.interarrival_dist();
    }

    absl::Duration run_dur;
    if (!absl::ParseDuration(config.run_dur(), &run_dur)) {
      LOG(FATAL) << "invalid run duration: " << config.run_dur();
    }
    hr_run_dur = absl::ToInt64Nanoseconds(run_dur);

    if (rec_interarrival) {
      interarrival_hist.emplace(InterarrivalConfig());
    }
  }

  std::vector<client_conn_t*> conns;
  std::deque<std::function<void(client_conn_t*)>> to_exec;

  proto::TestLopriClientConfig config;
  std::unique_ptr<StatsRecorder> stats_recorder;
  int64_t hr_run_dur;
  uv_loop_t* loop;
  absl::InsecureBitGen rng;
  uint64_t hr_next;
  double dist_param;
  bool issued_first_req;

  uint64_t rpc_id = 0;
  uint64_t hr_start_time;
  uint64_t hr_next_report_time;
  uint64_t hr_report_dur;
  struct sockaddr_in dst_addr;

  uint64_t hr_last_recorded_time;
  absl::optional<HdrHistogram> interarrival_hist;
  bool record_interarrival_called;

  bool tearing_down;
};

State* state;
int next_conn_id = 0;

client_conn_t* new_client_conn() {
  CHECK_GE(state->config.rpc_size_bytes(), 12);

  client_conn_t* c = static_cast<client_conn_t*>(malloc(sizeof(client_conn_t)));
  c->buf = static_cast<char*>(calloc(state->config.rpc_size_bytes(), 1));
  WriteU32LE(state->config.rpc_size_bytes() - 12, c->buf);
  if (kDebug) {
    LOG(INFO) << "init buf header = " << ToHex(c->buf, 12)
              << " for rpc payload size = " << state->config.rpc_size_bytes() - 12;
  }
  c->num_seen = 0;
  c->next_pos = 0;
  c->read_buf_size = 0;
  c->in_pool = false;
  c->conn_id = ++next_conn_id;
  return c;
}

void free_client_conn(client_conn_t* c) {
  free(c->buf);
  free(c);
}

bool AddConn(client_conn_t* conn) {
  if (conn->in_pool) {
    return state->conns.size() == state->config.num_conns();
  }

  if (!state->to_exec.empty()) {
    state->to_exec.front()(conn);
    state->to_exec.pop_front();
    return false;
  }

  state->conns.push_back(conn);
  conn->in_pool = true;
  return state->conns.size() == state->config.num_conns();
}

void WithConn(const std::function<void(client_conn_t*)>& func) {
  if (!state->conns.empty()) {
    client_conn_t* conn = state->conns.back();
    state->conns.pop_back();
    conn->in_pool = false;

    func(conn);
    return;
  }

  state->to_exec.push_back(func);
}

uint64_t NextSendTimeHighRes() {
  double wait_sec = 0;
  switch (state->config.interarrival_dist()) {
    case proto::Distribution::CONSTANT:
      wait_sec = state->dist_param;
      break;
    case proto::Distribution::UNIFORM:
      wait_sec = absl::Uniform(state->rng, 0, 2 * state->dist_param);
      break;
    case proto::Distribution::EXPONENTIAL:
      wait_sec = absl::Exponential(state->rng, state->dist_param);
      break;
    default:
      LOG(FATAL) << "unreachable";
  }
  state->hr_next += wait_sec * 1'000'000'000;  // to ns

  return state->hr_next;
}

uint64_t UvTimeoutUntil(uint64_t hr_time) {
  uint64_t now = uv_now(state->loop);
  if (now >= hr_time / 1'000'000) {
    return 0;
  }
  return (hr_time / 1'000'000) - now;
}

void OnWriteDone(uv_write_t* req, int status) {
  client_conn_t* conn = reinterpret_cast<client_conn_t*>(req->handle);
  if (status < 0) {
    absl::FPrintF(stderr, "write error %s\n", uv_strerror(status));
    --conn->next_pos;
    free(req);
    return;
  }

  if (conn->next_pos - conn->num_seen < kNumParallelRpcsPerConn) {
    AddConn(conn);  // else OnRead will add it back
  }
  free(req);
}

void RecordInterarrivalIssuedNow() {
  if (!state->interarrival_hist.has_value()) {
    return;
  }
  uint64_t now = uv_hrtime();
  if (!state->record_interarrival_called) {
    state->hr_last_recorded_time = now;
    state->record_interarrival_called = true;
    return;
  }
  state->interarrival_hist->RecordValue(now - state->hr_last_recorded_time);
  state->hr_last_recorded_time = now;
}

bool MaybeIssueRequest(CachedTime* time) {
  uint64_t hr_now = time->Get();
  if (hr_now < state->hr_next) {
    return false;  // not yet
  }

  if (hr_now > state->hr_start_time + state->hr_run_dur) {
    LOG(INFO) << "exiting event loop after "
              << static_cast<double>(hr_now - state->hr_start_time) / 1e9 << " sec";
    state->tearing_down = true;
    uv_stop(state->loop);
    auto st = state->stats_recorder->Close();
    if (!st.ok()) {
      LOG(FATAL) << "error while recording: " << st;
    }
    return false;
  } else if (hr_now > state->hr_next_report_time) {
    int step = (state->hr_next_report_time - state->hr_start_time) / state->hr_report_dur;
    LOG(INFO) << "gathering stats for step " << step;
    state->stats_recorder->DoneStep(absl::StrCat("step=", step));
    state->hr_next_report_time += state->hr_report_dur;
  }

  // Issue the request
  uint64_t rpc_id = ++state->rpc_id;
  WithConn([hr_now, rpc_id](client_conn_t* conn) {
    conn->hr_start_time[conn->next_pos % kNumParallelRpcsPerConn] = hr_now;
    conn->rpc_id[conn->next_pos % kNumParallelRpcsPerConn] = rpc_id;
    ++conn->next_pos;
    WriteU64LE(rpc_id, conn->buf + 4);
    if (kDebug) {
      VLOG(2) << "write(c=" << conn->conn_id << ") rpc id=" << rpc_id
              << " header=" << ToHex(conn->buf, 12);
    }
    conn->buffer = uv_buf_init(conn->buf, state->config.rpc_size_bytes());
    uv_write_t* req = static_cast<uv_write_t*>(malloc(sizeof(uv_write_t)));
    uv_write(req, reinterpret_cast<uv_stream_t*>(&conn->client), &conn->buffer, 1,
             OnWriteDone);
    RecordInterarrivalIssuedNow();
  });

  return true;
}

// Called on every iteration of the event loop
void OnCheckNextReq(uv_check_t* check) {
  CachedTime time;
  while (!state->tearing_down && MaybeIssueRequest(&time)) {
    /* issue all requests whose time has passed */
    state->hr_next = NextSendTimeHighRes();
  }
  if (state->tearing_down) {
    uv_check_stop(check);
  }
}

void OnNextReq(uv_timer_t* timer) {
  CachedTime time;
  if (!state->issued_first_req) {
    state->issued_first_req = true;
    LOG(INFO) << "starting to issue requests";
    state->stats_recorder->StartRecording();
    state->hr_next = time.Get();

    auto check = static_cast<uv_check_t*>(malloc(sizeof(uv_check_t)));
    uv_check_init(state->loop, check);
    uv_check_start(check, OnCheckNextReq);
  }

  while (!state->tearing_down && MaybeIssueRequest(&time)) {
    /* issue all requests whose time has passed */
    state->hr_next = NextSendTimeHighRes();
  }

  if (!state->tearing_down) {
    // Schedule issuing of next request
    uv_timer_start(timer, OnNextReq, UvTimeoutUntil(state->hr_next), 0);
  }
}

void AllocBuf(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  buf->base = static_cast<char*>(malloc(suggested_size));
  buf->len = suggested_size;
}

void OnReadAck(uv_stream_t* client_stream, ssize_t nread, const uv_buf_t* buf) {
  auto client = reinterpret_cast<client_conn_t*>(client_stream);
  if (nread < 0) {
    if (nread != UV_EOF) absl::FPrintF(stderr, "Read error %s\n", uv_err_name(nread));
    uv_read_stop(client_stream);
    return;
  }

  bool got_ack = false;
  auto record_rpc_latency = [&](uint64_t rpc_id) {
    CHECK_EQ(client->rpc_id[client->num_seen % kNumParallelRpcsPerConn], rpc_id)
        << "rpcs on a single connection were reordered!";
    state->stats_recorder->RecordRpc(
        state->config.rpc_size_bytes(),
        absl::Nanoseconds(
            uv_hrtime() -
            client->hr_start_time[client->num_seen % kNumParallelRpcsPerConn]));
    ++client->num_seen;
    got_ack = true;
  };

  char* b = buf->base;
  char* e = buf->base + nread;
  while (b < e) {
    int tocopy = std::min<int>(e - b, 8 - client->read_buf_size);
    memmove(client->read_buf + client->read_buf_size, b, tocopy);
    b += tocopy;
    client->read_buf_size += tocopy;

    if (client->read_buf_size == 8) {
      if (kDebug) {
        VLOG(2) << "read (c=" << client->conn_id
                << ") rpc id=" << ReadU64LE(client->read_buf)
                << " header=" << ToHex(client->read_buf, 8);
      }
      record_rpc_latency(ReadU64LE(client->read_buf));
      client->read_buf_size = 0;
    }
  }
  free(buf->base);
  if (got_ack) {
    AddConn(client);  // will not add if present already
  }
}

void OnConnect(uv_connect_t* req, int status) {
  if (uv_hrtime() > state->hr_start_time) {
    LOG(FATAL) << "took too long to connect";
  }

  if (status < 0) {
    LOG(ERROR) << "failed to connect; trying again";
    uv_tcp_init(state->loop, reinterpret_cast<uv_tcp_t*>(req->handle));

    uv_tcp_connect(req, reinterpret_cast<uv_tcp_t*>(req->handle),
                   reinterpret_cast<const struct sockaddr*>(&state->dst_addr), OnConnect);
    return;
  }

  LOG(INFO) << "connection established; adding to pool";

  auto client = reinterpret_cast<client_conn_t*>(req->handle);
  uv_read_start(reinterpret_cast<uv_stream_t*>(&client->client), AllocBuf, OnReadAck);

  if (AddConn(client)) {
    auto hr_timeout = UvTimeoutUntil(state->hr_start_time);
    // Add callback to start the requests.
    LOG(INFO) << "will wait for " << static_cast<double>(hr_timeout) / 1e3
              << " seconds to issue requests";
    uv_timer_t* timer = static_cast<uv_timer_t*>(malloc(sizeof(uv_timer_t)));
    uv_timer_init(state->loop, timer);
    uv_timer_start(timer, OnNextReq, hr_timeout, 0);
  }

  free(req);
}

int InitAndRunAt(absl::string_view server_addr_full, absl::Time start_time) {
  absl::string_view addr_view;
  int32_t port;
  auto st = ParseHostPort(server_addr_full, &addr_view, &port);
  if (!st.ok()) {
    LOG(FATAL) << "invalid host/port: " << server_addr_full << ": " << st;
  }
  std::string addr(addr_view);

  int64_t ns_until_start = absl::ToInt64Nanoseconds(start_time - absl::Now());
  if (ns_until_start < 0) {
    state->hr_start_time = uv_hrtime();
  } else {
    state->hr_start_time = uv_hrtime() + ns_until_start;
  }
  state->hr_next_report_time = state->hr_start_time + state->hr_report_dur;

  uv_ip4_addr(addr.c_str(), port, &state->dst_addr);

  // Start by creating connections.
  // Once all are created, the last will register a timer to start the run
  for (int i = 0; i < state->config.num_conns(); ++i) {
    uv_connect_t* req = static_cast<uv_connect_t*>(malloc(sizeof(uv_connect_t)));

    client_conn_t* client_conn = new_client_conn();
    uv_tcp_init(state->loop, &client_conn->client);

    uv_tcp_connect(req, &client_conn->client,
                   reinterpret_cast<const struct sockaddr*>(&state->dst_addr), OnConnect);
  }

  uv_run(state->loop, UV_RUN_DEFAULT);
  return 0;
}

}  // namespace
}  // namespace heyp

DEFINE_string(c, "config.textproto", "path to input config");
DEFINE_string(server, "127.0.0.1:7777", "address of server");
DEFINE_string(out, "testlopri-client.log", "path to log output");
DEFINE_string(start_time, "", "wait until this time to start the run");
DEFINE_string(interarrival, "",
              "path to write out interarrival distribution (optional, for validation)");

int ShardMain(int argc, char** argv, int shard_index, int num_shards) {
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

  heyp::proto::TestLopriClientConfig config;
  if (!heyp::ReadTextProtoFromFile(FLAGS_c, &config)) {
    std::cerr << "failed to parse config file\n";
    return 4;
  }
  config.set_target_average_bps(config.target_average_bps() / num_shards);

  heyp::state = new heyp::State(config, uv_default_loop(), std::move(*srec),
                                FLAGS_interarrival != "");
  int ret = heyp::InitAndRunAt(FLAGS_server, start_time);
  if (FLAGS_interarrival != "") {
    CHECK(heyp::WriteTextProtoToFile(
        heyp::state->interarrival_hist->ToProto(),
        absl::StrCat(FLAGS_interarrival, ".shard.", shard_index)))
        << "failed to write latency hist";
  }
  return ret;
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

  pid_t* pids = static_cast<pid_t*>(malloc(sizeof(pid_t) * num_shards));
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
