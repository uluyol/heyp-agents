#ifndef HEYP_CLUSTER_AGENT_CONTROLLER_H_
#define HEYP_CLUSTER_AGENT_CONTROLLER_H_

#include <cstdint>
#include <thread>

#include "absl/container/flat_hash_map.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/flows/aggregator.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/threads/lossy-queue.h"
#include "spdlog/spdlog.h"

namespace heyp {

class ClusterController {
 public:
  ClusterController(std::unique_ptr<FlowAggregator> aggregator,
                    std::unique_ptr<ClusterAllocator> allocator);

  void UpdateInfo(const proto::InfoBundle& info);
  void ComputeAndBroadcast();
  void EnableWaitForBroadcastCompletion();
  void WaitForBroadcastCompletion();

  class Listener;

  std::unique_ptr<Listener> RegisterListener(
      int64_t host_id, const std::function<void(proto::AllocBundle)>& on_new_bundle_func);

  class Listener {
   public:
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

   private:
    struct Mesg {
      proto::AllocBundle b;
      bool wait_completion_enabled = false;
    };

    Listener();

    int64_t host_id_;
    uint64_t lis_id_;
    ClusterController* controller_ = nullptr;
    std::thread write_thread_;
    LossyQueue<Mesg> bundle_q_;

    friend class ClusterController;
  };

 private:
  absl::Mutex state_mu_;
  std::unique_ptr<FlowAggregator> aggregator_ ABSL_GUARDED_BY(state_mu_);
  std::unique_ptr<ClusterAllocator> allocator_ ABSL_GUARDED_BY(state_mu_);
  spdlog::logger logger_;

  absl::Mutex broadcasting_mu_;
  uint64_t next_lis_id_ ABSL_GUARDED_BY(broadcasting_mu_);
  absl::flat_hash_map<int64_t, absl::flat_hash_map<uint64_t, LossyQueue<Listener::Mesg>*>>
      new_bundle_qs_ ABSL_GUARDED_BY(broadcasting_mu_);

  // Used for testing
  absl::Mutex broadcast_wait_mu_;
  int num_broadcast_completed_ = 0;
  int want_num_broadcast_completed_ = 0;
  bool enable_wait_for_broadcast_completion_ = false;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_CONTROLLER_H_
