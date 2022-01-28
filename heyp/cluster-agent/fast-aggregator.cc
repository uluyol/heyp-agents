#include "heyp/cluster-agent/fast-aggregator.h"

#include "heyp/log/spdlog.h"

namespace heyp {

std::vector<FastAggInfo> FastAggregator::ComputeTemplateAggInfo(
    const ClusterFlowMap<int64_t>* agg_flow_to_id) {
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

FastAggregator::FastAggregator(const ClusterFlowMap<int64_t>* agg_flow_to_id,
                               std::vector<ThresholdSampler> samplers)
    : agg_flow_to_id_(agg_flow_to_id),
      samplers_(std::move(samplers)),
      template_agg_info_(ComputeTemplateAggInfo(agg_flow_to_id_)),
      logger_(MakeLogger("fast-agg")) {
  for (int i = 0; i < kNumInfoShards; ++i) {
    active_info_shard_ids_[i].store(0);
  }
}

void FastAggregator::UpdateInfo(const proto::InfoBundle& info) {
  const int shard = static_cast<uint>(info.bundler().host_id()) % kNumInfoShards;
  update_counters_[shard].fetch_add(1);

  std::vector<Info> got;
  got.reserve(agg_flow_to_id_->size());
  for (const proto::FlowInfo& fi : info.flow_infos()) {
    auto id_iter = agg_flow_to_id_->find(fi.flow());
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

  int cur_id = 0;
  while (true) {
    cur_id = active_info_shard_ids_[shard].load();
    if (cur_id < 0) {
      continue;  // try again
    }
    if (active_info_shard_ids_[shard].compare_exchange_strong(cur_id, -1)) {
      break;
    }
  }
  // Now active_info_shard_ids_[shard] < 0: no one else can modify
  InfoShard& cur = info_shards_[shard][cur_id];

  for (Info i : got) {
    cur.infos.push_back(i);
  }
  cur.gens.push_back({
      .host_id = info.bundler().host_id(),
      .gen = info.gen(),
  });

  // Done with info_shards_[shard]
  active_info_shard_ids_[shard].store(cur_id, std::memory_order_seq_cst);
}

std::pair<std::vector<FastAggInfo>, std::vector<FastAggregator::PrioEstimators>>
FastAggregator::Aggregate(
    const FastAggregator::InfoShard& shard,
    const std::vector<HashingDowngradeSelector>& downgrade_selectors) {
  std::vector<PrioEstimators> volume_bps;
  std::vector<FastAggInfo> agg;
  agg.reserve(template_agg_info_.size());
  volume_bps.reserve(template_agg_info_.size());
  for (int i = 0; i < template_agg_info_.size(); ++i) {
    agg.push_back(FastAggInfo());
    agg.back().agg_id_ = i;
    volume_bps.push_back({
        .hipri = samplers_[i].NewAggUsageEstimator(),
        .lopri = samplers_[i].NewAggUsageEstimator(),
    });
  }

  for (const Info& info : shard.infos) {
    agg[info.agg_id].children_.push_back(ChildFlowInfo{
        .child_id = info.child_id,
        .volume_bps = info.volume_bps,
        .currently_lopri = info.currently_lopri,
    });
    if (downgrade_selectors.at(info.agg_id).IsLOPRI(info.child_id)) {
      volume_bps[info.agg_id].lopri.RecordSample(info.volume_bps);
    } else {
      volume_bps[info.agg_id].hipri.RecordSample(info.volume_bps);
    }
  }
  for (const auto gen : shard.gens) {
    for (FastAggInfo& ai : agg) {
      ai.info_gen_.push_back(gen);
    }
  }

  return {agg, volume_bps};
}

std::vector<FastAggInfo> FastAggregator::CollectSnapshot(
    Executor* exec, const std::vector<HashingDowngradeSelector>& downgrade_selectors) {
  std::string buf;
  for (int i = 0; i < kNumInfoShards; ++i) {
    int64_t cum = update_counters_[i].load();
    if (i == 0) {
      buf += absl::StrCat(cum - last_update_counters_[i]);
    } else {
      buf += absl::StrCat(", ", cum - last_update_counters_[i]);
    }
    last_update_counters_[i] = cum;
  }
  SPDLOG_LOGGER_INFO(&logger_, "early num infos per shard [{}]", buf);

  // Compute a std::vector<FastAggInfo> concurrently for each shard.
  // Aggregate into one final std::vector<FastAggInfo>.
  std::unique_ptr<TaskGroup> tasks = exec->NewTaskGroup();
  std::array<std::vector<FastAggInfo>, kNumInfoShards> parts;
  std::array<std::vector<PrioEstimators>, kNumInfoShards> parent_volume_bps;
  std::atomic<int64_t> num_infos;
  for (int i = 0; i < kNumInfoShards; ++i) {
    tasks->AddTaskNoStatus(
        [i, this, &num_infos, &parts, &parent_volume_bps, &downgrade_selectors] {
          // First swap the existing shard data with the other buffer
          int cur_id = -2;
          while (true) {
            cur_id = active_info_shard_ids_[i].load();
            if (cur_id < 0) {
              continue;  // try again
            }
            int next_id = (cur_id + 1) % 2;
            info_shards_[i][next_id].gens.clear();
            info_shards_[i][next_id].infos.clear();
            if (active_info_shard_ids_[i].compare_exchange_strong(cur_id, next_id)) {
              break;
            }
          }
          InfoShard& cur = info_shards_[i][cur_id];
          num_infos.fetch_add(cur.infos.size());

          // cur contains the shard data
          std::tie(parts[i], parent_volume_bps[i]) =
              this->Aggregate(cur, downgrade_selectors);
        });
  }

  tasks->WaitAllNoStatus();
  SPDLOG_LOGGER_INFO(&logger_, "processed {} infos from host-agents", num_infos.load());

  std::vector<FastAggInfo> combined = parts[0];
  int64_t num_agg = combined.size();
  int64_t num_all_agg_children = 0;
  for (int i = 0; i < combined.size(); ++i) {
    size_t num_children = 0;
    for (int part = 0; part < parts.size(); ++part) {
      num_children += parts[part][i].children_.size();
    }
    combined[i].children_.reserve(num_children);
    num_all_agg_children += num_children;

    // TODO finish and what about sampling again?
    for (int part = 1; part < parts.size(); ++part) {
      for (const ChildFlowInfo& child : parts[part][i].children_) {
        combined[i].children_.push_back(child);
      }
    }

    int64_t sum_bps = 0;
    int64_t sum_hipri_bps = 0;
    int64_t sum_lopri_bps = 0;
    for (int part = 0; part < parts.size(); ++part) {
      const int64_t hipri_part =
          parent_volume_bps[part][i].hipri.EstUsage(0 /* unused by ThresholdSampler */);
      const int64_t lopri_part =
          parent_volume_bps[part][i].lopri.EstUsage(0 /* unused by ThresholdSampler */);
      sum_hipri_bps += hipri_part;
      sum_lopri_bps += lopri_part;
      sum_bps += hipri_part + lopri_part;
    }
    combined[i].parent_ = template_agg_info_[i].parent_;
    // Just set both so that the whichever is selected is populated.
    combined[i].parent_.set_ewma_usage_bps(sum_bps);
    combined[i].parent_.set_ewma_hipri_usage_bps(sum_hipri_bps);
    combined[i].parent_.set_ewma_lopri_usage_bps(sum_lopri_bps);
    combined[i].parent_.set_predicted_demand_bps(sum_bps);
  }
  SPDLOG_LOGGER_INFO(&logger_, "produced {} aggregates with a combined {} children",
                     num_agg, num_all_agg_children);
  return combined;
}

}  // namespace heyp
