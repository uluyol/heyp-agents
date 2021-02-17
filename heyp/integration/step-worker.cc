#include "heyp/integration/step-worker.h"

#include <netinet/in.h>
#include <pthread.h>
#include <sys/_types/_socklen_t.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cstring>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"
#include "glog/logging.h"
#include "heyp/posix/strerror.h"

namespace heyp {
namespace testing {

absl::StatusOr<std::unique_ptr<HostWorker>> HostWorker::Create() {
  int serve_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (serve_fd == -1) {
    return absl::UnknownError(absl::StrCat("failed to create socket: ", StrError(errno)));
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(0);
  if (bind(serve_fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof addr)) {
    return absl::UnknownError(absl::StrCat("failed to bind socket: ", StrError(errno)));
  }

  socklen_t addr_len = sizeof addr;
  if (getsockname(serve_fd, reinterpret_cast<struct sockaddr*>(&addr), &addr_len)) {
    return absl::UnknownError(
        absl::StrCat("failed to discover serving port: ", StrError(errno)));
  }

  return absl::WrapUnique(new HostWorker(serve_fd, ntohs(addr.sin_port)));
}

void HostWorker::Serve() {
  // HostWorker* worker = static_cast<HostWorker*>(arg);
  constexpr int kListenBacklog = 10;
  if (listen(serve_fd_, kListenBacklog) == -1) {
    return;
  }

  while (true) {
    int connfd = accept(serve_fd_, nullptr, nullptr);
    if (connfd == -1) {
      break;
    }
    LOG(INFO) << "new connection on fd " << connfd;
    absl::MutexLock l(&mu_);
    worker_fds_.push_back(connfd);
    worker_threads_.push_back(std::thread(&HostWorker::RecvFlow, this,
                                          connfd));  // does not own connfd
  }
}

HostWorker::HostWorker(int serve_fd, int serve_port)
    : serve_fd_(serve_fd), serve_port_(serve_port) {
  serve_thread_ = std::thread(&HostWorker::Serve, this);
}

int HostWorker::serve_port() const { return serve_port_; }

namespace {
constexpr int kBufSize = 8096;

bool ReadFull(int fd, char* buf, int n) {
  while (n > 0) {
    int ret = read(fd, buf, n);
    if (ret == -1) {
      continue;  // failed to read, try again
    } else if (ret == 0) {
      return false;
    } else {
      buf += ret;
      n -= ret;
    }
  }
  return true;
}

}  // namespace

void HostWorker::RecvFlow(int fd) {
  char buf[kBufSize];
  // auto fd_closer = absl::MakeCleanup([fd] { close(fd); });

  VLOG(2) << "fd " << fd << ": read name length";
  if (!ReadFull(fd, buf, 2)) {
    return;
  }
  int name_size = buf[0] + (buf[1] << 8);
  VLOG(2) << "fd " << fd << ": read name";
  if (!ReadFull(fd, buf, name_size)) {
    return;
  }
  std::string name(buf, name_size);

  VLOG(2) << "fd " << fd << ": init counter";
  std::atomic<int64_t>* counter = nullptr;
  {
    absl::MutexLock l(&mu_);
    counter = GetCounter(name);
  }

  VLOG(2) << "fd " << fd << ": start read loop";
  while (!shutting_down_.load()) {
    int ret = read(fd, buf, sizeof buf);
    if (ret > 0) {
      counter->fetch_add(ret);
    }
  }
  VLOG(2) << "fd " << fd << ": exited read loop";
}

#ifdef __APPLE__
#define SEND_FLAGS 0
#else
#define SEND_FLAGS MSG_NOSIGNAL
#endif

void HostWorker::SendFlow(int fd, std::string name, std::atomic<int64_t>* counter) {
  // auto fd_closer = absl::MakeCleanup([fd] { close(fd); });
  char buf[kBufSize];
  buf[0] = name.size();
  buf[1] = name.size() >> 8;
  memmove(buf + 2, name.data(), name.size());
  if (send(fd, buf, name.size() + 2, SEND_FLAGS) == name.size() + 2) {
    VLOG(2) << "fd " << fd << ": waiting for go signal";
    go_.WaitForNotification();
    VLOG(2) << "fd " << fd << ": start write loop";
    while (!shutting_down_.load()) {
      int ret = send(fd, buf, kBufSize, SEND_FLAGS);
      if (ret > 0) {
        counter->fetch_add(ret);
      }
    }
    VLOG(2) << "fd " << fd << ": exited write loop";
  }
}

std::atomic<int64_t>* HostWorker::GetCounter(const std::string& name) {
  auto iter = counters_.find(name);
  if (iter == counters_.end()) {
    return counters_.insert({name, absl::make_unique<std::atomic<int64_t>>()})
        .first->second.get();
  }
  return iter->second.get();
}

absl::Status HostWorker::InitFlows(std::vector<Flow>& flows) {
  for (Flow& f : flows) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
      return absl::UnknownError(
          absl::StrCat("failed to create socket: ", StrError(errno)));
    }

#ifdef __APPLE__
    {
      int set = 1;
      if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, static_cast<void*>(&set),
                     sizeof set)) {
        return absl::UnknownError(
            absl::StrCat("failed to set NOSIGPIPE on socket: ", StrError(errno)));
      }
    }
#endif

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(f.dst_port);
    if (connect(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof addr)) {
      return absl::UnknownError(absl::StrCat("failed to connect: ", StrError(errno)));
    }

    struct sockaddr_in local_addr;
    local_addr.sin_family = AF_INET;
    socklen_t local_addr_len = sizeof local_addr;
    if (getsockname(fd, reinterpret_cast<struct sockaddr*>(&local_addr),
                    &local_addr_len)) {
      return absl::UnknownError(
          absl::StrCat("failed to get local port: ", StrError(errno)));
    }
    f.src_port = ntohs(local_addr.sin_port);

    absl::MutexLock l(&mu_);
    worker_fds_.push_back(fd);
    worker_threads_.push_back(std::thread(&HostWorker::SendFlow, this,
                                          fd /* does not own fd */, f.name,
                                          GetCounter(f.name)));
  }

  return absl::OkStatus();
}

void HostWorker::Go() {
  go_.Notify();
  last_step_time_ = absl::Now();
}

void HostWorker::CollectStep(const std::string& label) {
  absl::Time now = absl::Now();
  double elapsed_sec = absl::ToDoubleSeconds(now - last_step_time_);
  absl::MutexLock l(&mu_);

  for (const auto& key_counter_pair : counters_) {
    CounterAndBps& measured = measurements_[key_counter_pair.first];
    int64_t cur_cum_bytes = key_counter_pair.second->load();
    measured.step_bps.push_back(
        {label, 8 * (cur_cum_bytes - measured.cum_bytes) / elapsed_sec});
    measured.cum_bytes = cur_cum_bytes;
  }

  last_step_time_ = now;
}

HostWorker::~HostWorker() { ABSL_ASSERT(got_metrics_); }

std::vector<proto::TestCompareMetrics::Metric> HostWorker::Finish() {
  shutting_down_.store(true);

  if (serve_fd_ != -1) {
    LOG(INFO) << "tearing down serve loop";
    // shutdown(serve_fd_, SHUT_RDWR);
    close(serve_fd_);
    serve_thread_.join();
  }

  absl::MutexLock l(&mu_);
  LOG(INFO) << "tearing down " << worker_threads_.size() << " workers";
  for (int fd : worker_fds_) {
    VLOG(2) << "close fd " << fd;
    close(fd);
  }
  for (size_t i = 0; i < worker_threads_.size(); ++i) {
    VLOG(2) << "join worker " << i;
    if (!worker_threads_[i].joinable()) {
      LOG(ERROR) << "worker thread " << i << " is bad";
    }
    worker_threads_[i].join();
  }

  LOG(INFO) << "accumulating results";
  std::vector<proto::TestCompareMetrics::Metric> results;
  for (const auto& key_val_pair : measurements_) {
    for (const auto& label_step_bps_pair : key_val_pair.second.step_bps) {
      proto::TestCompareMetrics::Metric m;
      m.set_name(absl::StrCat(key_val_pair.first, "/", label_step_bps_pair.first));
      m.set_value(label_step_bps_pair.second);
      results.push_back(std::move(m));
    }
  }

  got_metrics_ = true;

  return results;
}

}  // namespace testing
}  // namespace heyp
