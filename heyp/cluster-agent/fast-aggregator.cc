#include "heyp/cluster-agent/fast-aggregator.h"

#include "heyp/flows/agg-marker.h"

namespace heyp {

std::vector<FastAggInfo> FastAggregator::ComputeTemplateAggInfo(
    const FlowMap<int64_t>* agg_flow_to_id) {
  std::vector<FastAggInfo> infos(agg_flow_to_id->size(), FastAggInfo{});
  for (int i = 0; i < infos.size(); ++i) {
    infos[i].agg_id_ = -1;
  }
  for (auto& [flow, id] : *agg_flow_to_id) {
    H_ASSERT_LT(id, infos.size());
    FastAggInfo& info = infos.at(id);
    info.agg_id_ = id;
    *info.parent_.mutable_flow() = flow;
  }
  for (int i = 0; i < infos.size(); ++i) {
    H_ASSERT_NE(infos[i].agg_id_, -1);
    H_ASSERT_EQ(infos[i].agg_id_, i);
  }
  return infos;
}

FastAggregator::FastAggregator(const FlowMap<int64_t>* agg_flow_to_id,
                               const std::vector<ThresholdSampler>* samplers)
    : agg_flow_to_id_(agg_flow_to_id),
      samplers_(samplers),
      template_agg_info_(ComputeTemplateAggInfo(agg_flow_to_id_)),
      last_shard_size_(64) {
  for (int i = 0; i < kNumInfoShards; ++i) {
    info_shards_[i] = new std::vector<Info>;
  }
}

void FastAggregator::UpdateInfo(ParID bundler_id, const proto::InfoBundle& info) {
  std::vector<Info> got;
  got.reserve(agg_flow_to_id_->size());
  for (const proto::FlowInfo& fi : info.flow_infos()) {
    auto id_iter = agg_flow_to_id_->find(ToClusterFlow(fi.flow()));
    if (id_iter == agg_flow_to_id_->end()) {
      continue;
    }
    got.push_back(Info{
        .agg_id = id_iter->second,
        .child_id = info.bundler().host_id(),
        .volume_bps = fi.ewma_usage_bps(),
        .currently_lopri = fi.currently_lopri(),
    });
  }

  int shard = static_cast<uint>(bundler_id) % kNumInfoShards;

  std::vector<Info>* cur = nullptr;
  while (true) {
    cur = info_shards_[shard].load();
    if (cur == &info_being_updated_) {
      continue;  // try again
    }
    if (std::atomic_compare_exchange_strong(&info_shards_[shard], &cur,
                                            &info_being_updated_)) {
      break;
    }
  }
  // Now info_shards_[shard] == &info_being_updated_: no one else can modify

  for (Info i : got) {
    cur->push_back(i);
  }

  // Done with info_shards_[shard]
  info_shards_[shard].store(cur, std::memory_order_seq_cst);
}

std::pair<std::vector<FastAggInfo>, std::vector<ThresholdSampler::AggUsageEstimator>>
FastAggregator::Aggregate(const std::vector<FastAggregator::Info>& shard) {
  std::vector<ThresholdSampler::AggUsageEstimator> volume_bps;
  std::vector<FastAggInfo> agg;
  agg.reserve(template_agg_info_.size());
  volume_bps.reserve(template_agg_info_.size());
  for (int i = 0; i < template_agg_info_.size(); ++i) {
    agg.push_back(FastAggInfo());
    agg.back().agg_id_ = i;
    volume_bps.push_back((*samplers_)[i].NewAggUsageEstimator());
  }

  for (const Info& info : shard) {
    agg[info.agg_id].children_.push_back(ChildFlowInfo{
        .child_id = info.child_id,
        .volume_bps = info.volume_bps,
        .currently_lopri = info.currently_lopri,
    });
    volume_bps[info.agg_id].RecordSample(info.volume_bps);
  }

  return {agg, volume_bps};
}

std::vector<FastAggInfo> FastAggregator::CollectSnapshot(Executor* exec) {
  // Compute a std::vector<FastAggInfo> concurrently for each shard.
  // Aggregate into one final std::vector<FastAggInfo>.
  std::unique_ptr<TaskGroup> tasks = exec->NewTaskGroup();
  std::array<std::vector<FastAggInfo>, kNumInfoShards> parts;
  std::array<size_t, kNumInfoShards> sizes;
  std::array<std::vector<ThresholdSampler::AggUsageEstimator>, kNumInfoShards>
      parent_volume_bps;
  for (int i = 0; i < kNumInfoShards; ++i) {
    tasks->AddTaskNoStatus([i, this, &parts, &sizes, &parent_volume_bps] {
      // First swap the existing shard data with a new buffer
      std::vector<Info>* new_shard_data = new std::vector<Info>;
      new_shard_data->resize(this->last_shard_size_);
      std::vector<Info>* cur = nullptr;
      while (true) {
        cur = info_shards_[i].load();
        if (cur == &info_being_updated_) {
          continue;  // try again
        }
        if (std::atomic_compare_exchange_strong(&info_shards_[i], &cur, new_shard_data)) {
          break;
        }
      }

      // cur contains the shard data
      H_ASSERT_NE(cur, nullptr);
      std::tie(parts[i], parent_volume_bps[i]) = this->Aggregate(*cur);
      sizes[i] = cur->size();
      delete cur;
    });
  }

  tasks->WaitAllNoStatus();

  for (size_t s : sizes) {
    if (s > last_shard_size_) {
      last_shard_size_ = s;
    }
  }

  std::vector<FastAggInfo> combined = parts[0];
  for (int i = 0; i < combined.size(); ++i) {
    size_t num_children = 0;
    for (int part = 0; part < parts.size(); ++part) {
      num_children += parts[part][i].children_.size();
    }
    combined[i].children_.reserve(num_children);

    // TODO finish and what about sampling again?
    for (int part = 1; part < parts.size(); ++part) {
      for (const ChildFlowInfo& child : parts[part][i].children_) {
        combined[i].children_.push_back(child);
      }
    }

    int64_t sum_bps = 0;
    for (int part = 0; part < parts.size(); ++part) {
      sum_bps += parent_volume_bps[part][i].EstUsage(0 /* unused by ThresholdSampler */);
    }
    combined[i].parent_ = template_agg_info_[i].parent_;
    // Just set both so that the whichever is selected is populated.
    combined[i].parent_.set_ewma_usage_bps(sum_bps);
    combined[i].parent_.set_predicted_demand_bps(sum_bps);
  }
  return combined;
}

}  // namespace heyp
