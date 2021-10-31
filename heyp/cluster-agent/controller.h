#ifndef HEYP_CLUSTER_AGENT_CONTROLLER_H_
#define HEYP_CLUSTER_AGENT_CONTROLLER_H_

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/flows/aggregator.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/threads/mutex-helpers.h"
#include "spdlog/spdlog.h"

namespace heyp {

class ClusterController {
 public:
  ClusterController(std::unique_ptr<FlowAggregator> aggregator,
                    std::unique_ptr<ClusterAllocator> allocator);

  void UpdateInfo(ParID bundler_id, const proto::InfoBundle& info);
  void ComputeAndBroadcast();
  void EnableWaitForBroadcastCompletion();
  void WaitForBroadcastCompletion();

  class Listener;

  // on_new_bundle_func should not block.
  std::unique_ptr<Listener> RegisterListener(
      int64_t host_id,
      const std::function<void(const proto::AllocBundle&)>& on_new_bundle_func);

  ParID GetBundlerID(const proto::FlowMarker& bundler);

  class Listener {
   public:
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

   private:
    Listener();

    int64_t host_id_;
    uint64_t lis_id_;
    std::function<void(const proto::AllocBundle&)> on_new_bundle_func_;
    ClusterController* controller_ = nullptr;

    friend class ClusterController;
  };

 private:
  std::unique_ptr<FlowAggregator> aggregator_;
  TimedMutex state_mu_;
  std::unique_ptr<ClusterAllocator> allocator_ ABSL_GUARDED_BY(state_mu_);
  spdlog::logger logger_;

  TimedMutex broadcasting_mu_;
  uint64_t next_lis_id_ ABSL_GUARDED_BY(broadcasting_mu_);
  absl::flat_hash_map<
      int64_t,
      absl::flat_hash_map<uint64_t, std::function<void(const proto::AllocBundle&, bool)>>>
      new_bundle_funcs_ ABSL_GUARDED_BY(broadcasting_mu_);

  // Used for testing
  absl::Mutex broadcast_wait_mu_;
  int num_broadcast_completed_ = 0;
  int want_num_broadcast_completed_ = 0;
  bool enable_wait_for_broadcast_completion_ = false;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_CONTROLLER_H_
