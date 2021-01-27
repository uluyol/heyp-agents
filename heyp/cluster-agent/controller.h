#ifndef HEYP_CLUSTER_AGENT_CONTROLLER_H_
#define HEYP_CLUSTER_AGENT_CONTROLLER_H_

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/flows/aggregator.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

class ClusterController {
 public:
  ClusterController(std::unique_ptr<FlowAggregator> aggregator,
                    std::unique_ptr<ClusterAllocator> allocator);

  void UpdateInfo(const proto::InfoBundle& info);
  void ComputeAndBroadcast();

  class Listener;

  Listener RegisterListener(
      int64_t host_id,
      const std::function<void(proto::AllocBundle)>& on_new_bundle_func);

  class Listener {
   public:
    Listener();

    ~Listener();
    Listener(Listener&& other);
    Listener& operator=(Listener&& other);

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

   private:
    int64_t host_id_ = 0;
    ClusterController* controller_ = nullptr;

    friend class ClusterController;
  };

 private:
  absl::Mutex state_mu_;
  std::unique_ptr<FlowAggregator> aggregator_ ABSL_GUARDED_BY(state_mu_);
  std::unique_ptr<ClusterAllocator> allocator_ ABSL_GUARDED_BY(state_mu_);

  absl::Mutex broadcasting_mu_;
  absl::flat_hash_map<int64_t, std::function<void(proto::AllocBundle)>>
      on_new_bundle_funcs_ ABSL_GUARDED_BY(broadcasting_mu_);
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_CONTROLLER_H_
