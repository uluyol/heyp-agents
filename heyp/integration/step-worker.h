#ifndef HEYP_INTEGRATION_STEP_WORKER_H_
#define HEYP_INTEGRATION_STEP_WORKER_H_

#include <pthread.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "heyp/proto/integration.pb.h"

namespace heyp {
namespace testing {

class HostWorker {
 public:
  static absl::StatusOr<std::unique_ptr<HostWorker>> Create();
  ~HostWorker();

  int serve_port() const;

  struct Flow {
    std::string name;
    int src_port = 0;  // set by InitFlows
    int dst_port = 0;
  };
  absl::Status InitFlows(std::vector<Flow> &flows);
  void Go();

  void CollectStep(const std::string &label);

  std::vector<proto::TestCompareMetrics::Metric> Finish();

 private:
  HostWorker(int serve_fd, int serve_port);

  // RecvFlow and SendFlow do not take ownership over fd.
  void RecvFlow(int fd);
  void SendFlow(int fd, std::string name, std::atomic<int64_t> *counter);

  void Serve();

  const int serve_fd_;
  const int serve_port_;

  bool got_metrics_ = false;
  absl::Notification go_;

  std::atomic<bool> shutting_down_ = false;
  std::thread serve_thread_;

  absl::Time last_step_time_;

  struct CounterAndBps {
    int64_t cum_bytes = 0;
    std::vector<std::pair<std::string, double>> step_bps;
  };

  absl::Mutex mu_;
  absl::flat_hash_map<std::string, CounterAndBps> measurements_ ABSL_GUARDED_BY(mu_);
  std::vector<int> worker_fds_ ABSL_GUARDED_BY(mu_);
  std::vector<std::thread> worker_threads_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<std::string, std::unique_ptr<std::atomic<int64_t>>> counters_
      ABSL_GUARDED_BY(mu_);

  std::atomic<int64_t> *GetCounter(const std::string &name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
};

}  // namespace testing
}  // namespace heyp

#endif  // HEYP_INTEGRATION_STEP_WORKER_H_
