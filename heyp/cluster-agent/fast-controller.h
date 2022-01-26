#ifndef HEYP_CLUSTER_AGENT_FAST_CONTROLLER_H_
#define HEYP_CLUSTER_AGENT_FAST_CONTROLLER_H_

#include <atomic>

#include "absl/container/btree_map.h"
#include "absl/functional/function_ref.h"
#include "heyp/alg/downgrade/impl-hashing.h"
#include "heyp/alg/sampler.h"
#include "heyp/alg/unordered-ids.h"
#include "heyp/cluster-agent/controller-iface.h"
#include "heyp/cluster-agent/fast-aggregator.h"
#include "heyp/cluster-agent/per-agg-allocators/util.h"
#include "heyp/flows/map.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/threads/executor.h"
#include "heyp/threads/par-indexed-map.h"

namespace heyp {

// FastController is a controller that can quickly perform downgrade
// using sampling and (mostly or fully) demand-oblivious downgrade selection.
//
// It is not as full featured as ClusterController.s
class FastClusterController : public ClusterController {
 public:
  static std::unique_ptr<FastClusterController> Create(
      const proto::FastClusterControllerConfig& config,
      const proto::AllocBundle& cluster_wide_allocs);

  void UpdateInfo(ParID bundler_id, const proto::InfoBundle& info) override {
    aggregator_.UpdateInfo(info);
  }

  void ComputeAndBroadcast() override;

  class Listener;

  // on_new_bundle_func should not block.
  std::unique_ptr<ClusterController::Listener> RegisterListener(
      uint64_t host_id, const OnNewBundleFunc& on_new_bundle_func) override;

  ParID GetBundlerID(const proto::FlowMarker& bundler) override {
    return 0; /* currently unused by FastClusterController */
  }

  class Listener : public ClusterController::Listener {
   public:
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

   private:
    Listener();

    ParID host_par_id_;
    uint64_t lis_id_;
    FastClusterController* controller_ = nullptr;

    friend class FastClusterController;
  };

 private:
  FastClusterController(const proto::FastClusterControllerConfig& config,
                        ClusterFlowMap<int64_t> agg_flow2id,
                        std::vector<proto::FlowMarker> agg_id2flow,
                        std::vector<int64_t> approval_bps,
                        std::vector<ThresholdSampler> samplers, int num_threads);

  const ClusterFlowMap<int64_t> agg_flow2id_;
  const std::vector<proto::FlowMarker> agg_id2flow_;
  const std::vector<int64_t> approval_bps_;

  spdlog::logger logger_;
  Executor exec_;
  FastAggregator aggregator_;

  struct PerAggState {
    double downgrade_frac = 0;
    double ewma_max_child_usage = -1;
    std::optional<DowngradeFracController> frac_controller;
  };

  std::vector<PerAggState> agg_states_;
  std::vector<HashingDowngradeSelector> agg_selectors_;

  std::atomic<uint64_t> next_lis_id_;
  struct ChildState {
    std::vector<bool> agg_is_lopri;
    absl::flat_hash_map<uint64_t, OnNewBundleFunc> lis_new_bundle_funcs;
    int64_t gen_seen = 0;
    bool broadcasted_latest_state = false;
    bool saw_data_this_run = false;
  };
  ParIndexedMap<uint64_t, ChildState, absl::flat_hash_map<uint64_t, ParID>> child_states_;

  // base_bundle should have flows_allocs[i].flow populated for all aggregate flows.
  void BroadcastStateUnconditional(const SendBundleAux& aux,
                                   proto::AllocBundle* base_bundle, ChildState& state);
  void BroadcastStateIfUpdated(const SendBundleAux& aux, proto::AllocBundle* base_bundle,
                               ChildState& state);

  // A copy of the id map in child_states_ that is maintained so that we don't need to
  // synchronize between UpdateInfo and ComputeAndBroadcast.
  // Additionally, we can specialize the respresentations to serve each use the best.
  //
  // Only used by ComputeAndBroadcast.
  absl::btree_map<uint64_t, ParID> host2par_;

  // A list of new host ID information that will be drained and added to host2par_ when
  // ComputeAndBroadcast runs.
  //
  // Pushed to in RegisterListener and drained in ComputeAndBroadcast.
  absl::Mutex mu_;
  std::vector<std::pair<uint64_t, ParID>> new_host_id_pairs_ ABSL_GUARDED_BY(mu_);
};

// Time complexity: O(log(id2par) * (ids.ranges + ids.points))
void ForEachSelected(const absl::btree_map<uint64_t, ParID>& id2par, UnorderedIds ids,
                     absl::FunctionRef<void(uint64_t, ParID)> func);

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_FAST_CONTROLLER_H_
