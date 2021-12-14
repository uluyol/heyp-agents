#ifndef HEYP_CLUSTER_AGENT_FAST_AGGREGATOR_H_
#define HEYP_CLUSTER_AGENT_FAST_AGGREGATOR_H_

#include <atomic>
#include <cstdint>

#include "heyp/alg/agg-info-views.h"
#include "heyp/alg/sampler.h"
#include "heyp/flows/map.h"
#include "heyp/threads/executor.h"

namespace heyp {

class FastAggInfo : public AggInfoView {
 public:
  const proto::FlowInfo& parent() const override { return parent_; }
  const std::vector<ChildFlowInfo>& children() const override { return children_; }

  int64_t agg_id() const { return agg_id_; }

 private:
  proto::FlowInfo parent_;
  std::vector<ChildFlowInfo> children_;
  int64_t agg_id_ = -1;

  friend class FastAggregator;
};

class FastAggregator {
 public:
  FastAggregator(const FlowMap<int64_t>* agg_flow_to_id,
                 const std::vector<ThresholdSampler>* samplers);

  // UpdateInfo updates the info. This method is thread safe.
  void UpdateInfo(const proto::InfoBundle& info);

  // CollectSnapshot produces a snapshot of usage. It should only be called from
  // one thread at a time but it may be called in parallel to UpdateInfo.
  std::vector<FastAggInfo> CollectSnapshot(Executor* exec);

 private:
  struct Info {
    int64_t agg_id;
    uint64_t child_id;
    int64_t volume_bps;
    bool currently_lopri;
  };

  static std::vector<FastAggInfo> ComputeTemplateAggInfo(
      const FlowMap<int64_t>* agg_flow_to_id);
  // Aggregate aggregates the info but doesn't populate parent_.
  std::pair<std::vector<FastAggInfo>, std::vector<ThresholdSampler::AggUsageEstimator>>
  Aggregate(const std::vector<Info>& shard);

  const FlowMap<int64_t>* agg_flow_to_id_;
  const std::vector<ThresholdSampler>* samplers_;
  const std::vector<FastAggInfo> template_agg_info_;

  constexpr static int kNumInfoShards = 8;
  // Maintain a sharded queues of Infos.
  // The current data for shard i can be found in info_shard_[active_info_shard_id_[i]]
  // *if* active_info_shard_id_[i] > 0. If it is < 0, a write is in progress.
  std::array<std::atomic<int>, kNumInfoShards> active_info_shard_ids_;
  std::array<std::array<std::vector<Info>, 2>, kNumInfoShards> info_shards_;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_FAST_AGGREGATOR_H_
