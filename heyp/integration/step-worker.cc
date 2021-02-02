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

namespace heyp {
namespace testing {

absl::StatusOr<std::unique_ptr<HostWorker>> HostWorker::Create() {
  int serve_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (serve_fd == -1) {
    return absl::UnknownError(
        absl::StrCat("failed to create socket: errno(", errno, ")"));
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_LOOPBACK;
  addr.sin_port = htons(0);
  if (bind(serve_fd, reinterpret_cast<const struct sockaddr*>(&addr),
           sizeof addr)) {
    return absl::UnknownError(
        absl::StrCat("failed to bind socket: errno(", errno, ")"));
  }

  socklen_t addr_len = sizeof addr;
  if (getsockname(serve_fd, reinterpret_cast<struct sockaddr*>(&addr),
                  &addr_len)) {
    return absl::UnknownError(
        absl::StrCat("failed to discover serving port: errno(", errno, ")"));
  }

  return absl::WrapUnique(new HostWorker(serve_fd, ntohs(addr.sin_port)));
}

void* HostWorkerServe(void* arg) {
  HostWorker* worker = static_cast<HostWorker*>(arg);
  constexpr int kListenBacklog = 10;
  if (listen(worker->serve_fd_, kListenBacklog) == -1) {
    return nullptr;
  }

  while (true) {
    int connfd = accept(worker->serve_fd_, nullptr, nullptr);
    if (connfd == -1) {
      continue;
    }
    absl::MutexLock l(&worker->mu_);
    worker->worker_threads_.push_back(
        std::thread(&HostWorker::RecvFlow, worker, connfd));
  }
}

HostWorker::HostWorker(int serve_fd, int serve_port)
    : serve_fd_(serve_fd), serve_port_(serve_port) {
  pthread_create(&serve_thread_, nullptr, HostWorkerServe,
                 static_cast<void*>(this));
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
  auto fd_closer = absl::MakeCleanup([fd] { close(fd); });

  if (!ReadFull(fd, buf, 2)) {
    return;
  }
  int name_size = buf[0] + (buf[1] << 8);
  if (!ReadFull(fd, buf, name_size)) {
    return;
  }
  std::string name(buf, name_size);

  std::atomic<int64_t>* counter = nullptr;
  {
    absl::MutexLock l(&mu_);
    counter = GetCounter(name);
  }

  int ret = -1;
  do {
    ret = read(fd, buf, sizeof buf);
    if (ret > 0) {
      counter->fetch_add(ret);
    }
  } while (ret > 0);
}

void HostWorker::SendFlow(int fd, std::string name,
                          std::atomic<int64_t>* counter) {
  auto fd_closer = absl::MakeCleanup([fd] { close(fd); });
  char buf[kBufSize];
  buf[0] = name.size();
  buf[1] = name.size() >> 8;
  memmove(buf + 2, name.data(), name.size());
  if (!write(fd, buf, name.size() + 2)) {
    go_.WaitForNotification();
    while (!shutting_down_.load()) {
      int ret = write(fd, buf, kBufSize);
      if (ret > 0) {
        counter->fetch_add(ret);
      }
    }
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
          absl::StrCat("failed to create socket: errno(", errno, ")"));
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_LOOPBACK;
    addr.sin_port = htons(f.dst_port);
    if (connect(fd, reinterpret_cast<const struct sockaddr*>(&addr),
                sizeof addr)) {
      return absl::UnknownError(
          absl::StrCat("failed to connect: errno(", errno, ")"));
    }

    struct sockaddr_in local_addr;
    local_addr.sin_family = AF_INET;
    socklen_t local_addr_len = sizeof local_addr;
    if (getsockname(fd, reinterpret_cast<struct sockaddr*>(&local_addr),
                    &local_addr_len)) {
      return absl::UnknownError(
          absl::StrCat("failed to get local port: errno(", errno, ")"));
    }
    f.src_port = ntohs(local_addr.sin_port);

    absl::MutexLock l(&mu_);
    worker_threads_.push_back(std::thread(&HostWorker::SendFlow, this,
                                          fd /* owns fd */, f.name,
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
    int64_t cum_bytes = key_counter_pair.second->load();
    measured.step_bps.push_back(
        {label, 8 * (measured.cum_bytes - cum_bytes) / elapsed_sec});
    measured.cum_bytes = cum_bytes;
  }

  last_step_time_ = now;
}

HostWorker::~HostWorker() { ABSL_ASSERT(got_metrics_); }

std::vector<proto::TestCompareMetrics::Metric> HostWorker::Finish() {
  shutting_down_.store(true);

  if (serve_fd_ != -1) {
    close(serve_fd_);
    pthread_kill(serve_thread_, SIGINT);
    pthread_join(serve_thread_, nullptr);
  }

  absl::MutexLock l(&mu_);
  for (auto& t : worker_threads_) {
    t.join();
  }

  std::vector<proto::TestCompareMetrics::Metric> results;
  for (const auto& key_val_pair : measurements_) {
    for (const auto& label_step_bps_pair : key_val_pair.second.step_bps) {
      proto::TestCompareMetrics::Metric m;
      m.set_name(
          absl::StrCat(key_val_pair.first, "/", label_step_bps_pair.first));
      m.set_value(label_step_bps_pair.second);
      results.push_back(std::move(m));
    }
  }

  got_metrics_ = true;

  return results;
}

}  // namespace testing
}  // namespace heyp
