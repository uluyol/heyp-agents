#include "heyp/cluster-agent/fg-tracker.h"

#include <algorithm>

#include "glog/logging.h"
#include "heyp/proto/constructors.h"

namespace heyp {

ClusterFGTracker::ClusterFGTracker(
    std::unique_ptr<DemandPredictor> cluster_demand_predictor,
    std::unique_ptr<DemandPredictor> host_demand_predictor, Config config)
    : config_(config),
      cluster_demand_predictor_(std::move(cluster_demand_predictor)),
      host_demand_predictor_(std::move(host_demand_predictor)) {}

void ClusterFGTracker::RemoveHost(int64_t host_id) {
  host_states_.erase(host_id);
}

void ClusterFGTracker::UpdateHost(const proto::HostInfo& host_info) {
  const absl::Time timestamp = FromProtoTimestamp(host_info.timestamp());
  HostState& host_state = host_states_[host_info.host_id()];
  ++host_state.gen;
  for (const proto::FlowInfo& flow_info : host_info.flow_infos()) {
    // TODO: check that host-agent aggregates
    CHECK_EQ(flow_info.marker().src_addr(), "");
    CHECK_EQ(flow_info.marker().dst_addr(), "");
    CHECK_EQ(flow_info.marker().protocol(), proto::Protocol::UNSET);
    CHECK_EQ(flow_info.marker().src_port(), 0);
    CHECK_EQ(flow_info.marker().dst_port(), 0);
    CHECK_EQ(flow_info.marker().seqnum(), 0);

    auto iter = host_state.agg_states.find(flow_info.marker());
    if (iter == host_state.agg_states.end()) {
      bool ok = false;
      std::tie(iter, ok) = host_state.agg_states.insert(
          {flow_info.marker(), HostAggState{
                                   .state = FlowState(flow_info.marker()),
                                   .cum_hipri_usage_bytes = 0,
                                   .cum_lopri_usage_bytes = 0,
                               }});
      ABSL_ASSERT(ok);
    }

    HostAggState& agg_state = iter->second;
    agg_state.state.UpdateUsage(
        {
            .time = timestamp,
            .cum_usage_bytes = flow_info.cum_usage_bytes(),
            .instantaneous_usage_bps = flow_info.ewma_usage_bps(),
        },
        config_.host_usage_history_window, *host_demand_predictor_);

    {
      AggState& cluster_state = GetAggState(flow_info.marker());
      cluster_state.cum_hipri_usage_bytes +=
          flow_info.cum_hipri_usage_bytes() - agg_state.cum_hipri_usage_bytes;
      cluster_state.cum_lopri_usage_bytes +=
          flow_info.cum_lopri_usage_bytes() - agg_state.cum_lopri_usage_bytes;
    }

    agg_state.cum_hipri_usage_bytes = flow_info.cum_hipri_usage_bytes();
    agg_state.cum_lopri_usage_bytes = flow_info.cum_lopri_usage_bytes();
    agg_state.gen = host_state.gen;
  }
  std::vector<proto::FlowMarker> to_erase;
  for (auto& iter : host_state.agg_states) {
    if (iter.second.gen < host_state.gen) {
      to_erase.push_back(iter.first);
    }
  }
  for (const proto::FlowMarker& marker : to_erase) {
    host_state.agg_states.erase(marker);
  }
}

std::vector<ClusterFGState> ClusterFGTracker::CollectSnapshot(absl::Time time) {
  // agg_states_[.*].cum_[lo|hi]pri_usage_bytes are updated in UpdateHost.
  // Need to compute sum_ewma_usage_bps, update state, and collect per-host
  // states.
  for (auto& marker_agg_state_pair : agg_states_) {
    marker_agg_state_pair.second.sum_ewma_usage_bps = 0;
    marker_agg_state_pair.second.host_info.clear();
  }

  for (const auto& host_state_pair : host_states_) {
    for (const auto& marker_agg_state_pair :
         host_state_pair.second.agg_states) {
      AggState& agg_state = agg_states_.at(marker_agg_state_pair.first);
      agg_state.sum_ewma_usage_bps +=
          marker_agg_state_pair.second.state.ewma_usage_bps();
      agg_state.host_info.push_back(marker_agg_state_pair.second.state);
    }
  }

  std::vector<ClusterFGState> cluster_fg_states;
  for (auto& marker_agg_state_pair : agg_states_) {
    AggState& agg_state = marker_agg_state_pair.second;
    agg_state.state.UpdateUsage(
        {
            .time = time,
            .cum_usage_bytes = agg_state.cum_hipri_usage_bytes +
                               agg_state.cum_lopri_usage_bytes,
            .instantaneous_usage_bps = agg_state.sum_ewma_usage_bps,
        },
        config_.cluster_usage_history_window, *host_demand_predictor_);
    std::sort(agg_state.host_info.begin(), agg_state.host_info.end(),
              [](const FlowState& lhs, const FlowState& rhs) {
                return lhs.flow().host_id() < rhs.flow().host_id();
              });
    cluster_fg_states.push_back({
        .state = agg_state.state,
        .cum_hipri_usage_bytes = agg_state.cum_hipri_usage_bytes,
        .cum_lopri_usage_bytes = agg_state.cum_lopri_usage_bytes,
        .host_info = std::move(agg_state.host_info),
    });
  }
  return cluster_fg_states;
}

ClusterFGTracker::AggState& ClusterFGTracker::GetAggState(
    proto::FlowMarker flow_marker) {
  auto iter = agg_states_.find(flow_marker);
  if (iter == agg_states_.end()) {
    bool ok = false;
    std::tie(iter, ok) = agg_states_.insert({
        flow_marker,
        AggState{.state = FlowState(flow_marker)},
    });
    ABSL_ASSERT(ok);
  }
  return iter->second;
}

}  // namespace heyp
