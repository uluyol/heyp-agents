#ifndef HEYP_CLUSTER_AGENT_FULL_CONTROLLER_H_
#define HEYP_CLUSTER_AGENT_FULL_CONTROLLER_H_

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/flows/aggregator.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/threads/mutex-helpers.h"
#include "spdlog/spdlog.h"

namespace heyp {

class FullClusterController {
 public:
  FullClusterController(std::unique_ptr<FlowAggregator> aggregator,
                        std::unique_ptr<ClusterAllocator> allocator);

  void UpdateInfo(ParID bundler_id, const proto::InfoBundle& info);
  void ComputeAndBroadcast();

  class Listener;

  // on_new_bundle_func should not block.
  std::unique_ptr<Listener> RegisterListener(
      uint64_t host_id,
      const std::function<void(const proto::AllocBundle&)>& on_new_bundle_func);

  ParID GetBundlerID(const proto::FlowMarker& bundler);

  class Listener {
   public:
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

   private:
    Listener();

    uint64_t host_id_;
    uint64_t lis_id_;
    FullClusterController* controller_ = nullptr;

    friend class FullClusterController;
  };

 private:
  std::unique_ptr<FlowAggregator> aggregator_;
  TimedMutex state_mu_;
  std::unique_ptr<ClusterAllocator> allocator_ ABSL_GUARDED_BY(state_mu_);
  spdlog::logger logger_;

  TimedMutex broadcasting_mu_;
  uint64_t next_lis_id_ ABSL_GUARDED_BY(broadcasting_mu_);
  absl::flat_hash_map<
      uint64_t,
      absl::flat_hash_map<uint64_t, std::function<void(const proto::AllocBundle&)>>>
      new_bundle_funcs_ ABSL_GUARDED_BY(broadcasting_mu_);
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_FULL_CONTROLLER_H_
