#include "heyp/flows/aggregator.h"

#include <algorithm>

#include "heyp/log/spdlog.h"
#include "heyp/proto/alg.h"
#include "heyp/proto/constructors.h"
#include "heyp/threads/mutex-helpers.h"

namespace heyp {
namespace {

CompareFlowOptions HostFlowOptions() {
  return CompareFlowOptions{
      .cmp_fg = true,
      .cmp_src_host = true,
      .cmp_host_flow = false,
      .cmp_seqnum = false,
  };
}

CompareFlowOptions ClusterFlowOptions() {
  return CompareFlowOptions{
      .cmp_fg = true,
      .cmp_src_host = false,
      .cmp_host_flow = false,
      .cmp_seqnum = false,
  };
}

}  // namespace

std::unique_ptr<FlowAggregator> NewConnToHostAggregator(
    std::unique_ptr<DemandPredictor> host_demand_predictor,
    absl::Duration usage_history_window) {
  return absl::make_unique<FlowAggregator>(
      std::move(host_demand_predictor),
      FlowAggregator::Config{
          .usage_history_window = usage_history_window,
          .get_agg_flow_fn = [](const proto::FlowMarker& c) -> proto::FlowMarker {
            proto::FlowMarker h = c;
            h.clear_src_addr();
            h.clear_dst_addr();
            h.clear_protocol();
            h.clear_src_port();
            h.clear_dst_port();
            h.clear_seqnum();
            return h;
          },
          .is_valid_parent = [](const proto::FlowMarker& h) -> bool {
            return ExpectedFieldsAreSet(h, HostFlowOptions()) &&
                   UnexpectedFieldsAreUnset(h, HostFlowOptions());
          },
          .is_valid_child = [](const proto::FlowMarker& c) -> bool {
            return ExpectedFieldsAreSet(c, {});
          },
      });
}

std::unique_ptr<FlowAggregator> NewHostToClusterAggregator(
    std::unique_ptr<DemandPredictor> cluster_demand_predictor,
    absl::Duration usage_history_window) {
  return absl::make_unique<FlowAggregator>(
      std::move(cluster_demand_predictor),
      FlowAggregator::Config{
          .usage_history_window = usage_history_window,
          .get_agg_flow_fn = [](const proto::FlowMarker& c) -> proto::FlowMarker {
            proto::FlowMarker h = c;
            h.clear_host_id();
            h.clear_src_addr();
            h.clear_dst_addr();
            h.clear_protocol();
            h.clear_src_port();
            h.clear_dst_port();
            h.clear_seqnum();
            return h;
          },
          .is_valid_parent = [](const proto::FlowMarker& c) -> bool {
            return ExpectedFieldsAreSet(c, ClusterFlowOptions()) &&
                   UnexpectedFieldsAreUnset(c, ClusterFlowOptions());
          },
          .is_valid_child = [](const proto::FlowMarker& h) -> bool {
            return ExpectedFieldsAreSet(h, HostFlowOptions());
          },
      });
}

FlowAggregator::FlowAggregator(std::unique_ptr<DemandPredictor> agg_demand_predictor,
                               Config config)
    : config_(std::move(config)),
      agg_demand_predictor_(std::move(agg_demand_predictor)),
      logger_(MakeLogger("flow-aggregator")) {}

void FlowAggregator::Update(const proto::InfoBundle& bundle) {
  const absl::Time timestamp = FromProtoTimestamp(bundle.timestamp());

  MutexLockWarnLong l(&mu_, absl::Seconds(1), &logger_, "mu_");
  BundleState& bs = bundle_states_[bundle.bundler()];
  for (const proto::FlowInfo& fi : bundle.flow_infos()) {
    if (config_.is_valid_child != nullptr) {
      H_SPDLOG_CHECK_MESG(&logger_, config_.is_valid_child(fi.flow()),
                          fi.flow().ShortDebugString());
    }

    auto iter = bs.active.find(fi.flow());
    if (iter == bs.active.end()) {
      // Remove from the dead map (in case it exists)
      bs.dead.erase(fi.flow());
      bs.active[fi.flow()] = {timestamp, fi};
    } else {
      iter->second = {timestamp, fi};
    }
  }
  std::vector<proto::FlowMarker> to_erase;
  for (const auto& iter : bs.active) {
    if (iter.second.first + config_.usage_history_window < timestamp) {
      to_erase.push_back(iter.first);
    }
  }
  for (const proto::FlowMarker& m : to_erase) {
    bs.dead[m] = {timestamp, bs.active[m].second};
    bs.active.erase(m);
  }
}

void FlowAggregator::ForEachAgg(
    absl::FunctionRef<void(absl::Time, const proto::AggInfo&)> func) {
  MutexLockWarnLong l(&mu_, absl::Seconds(1), &logger_, "mu_");

  for (auto& p : agg_wips_) {
    AggWIP& wip = p.second;
    wip.oldest_active_time = absl::InfiniteFuture();
    wip.newest_dead_time = absl::InfinitePast();
    wip.cum_hipri_usage_bytes = 0;
    wip.cum_lopri_usage_bytes = 0;
    wip.sum_ewma_usage_bps = 0;
    wip.children.clear();
  }

  for (const auto& bundle_pair : bundle_states_) {
    const BundleState& bs = bundle_pair.second;
    for (const auto& flow_time_info : bs.active) {
      AggWIP& wip = GetAggWIP(flow_time_info.first);
      wip.oldest_active_time =
          std::min(wip.oldest_active_time, flow_time_info.second.first);
      wip.cum_hipri_usage_bytes += flow_time_info.second.second.cum_hipri_usage_bytes();
      wip.cum_lopri_usage_bytes += flow_time_info.second.second.cum_lopri_usage_bytes();
      wip.sum_ewma_usage_bps += flow_time_info.second.second.ewma_usage_bps();
      wip.children.push_back(flow_time_info.second.second);
    }

    for (const auto& flow_time_info : bs.dead) {
      AggWIP& wip = GetAggWIP(flow_time_info.first);
      wip.newest_dead_time = std::max(wip.newest_dead_time, flow_time_info.second.first);
      wip.cum_hipri_usage_bytes += flow_time_info.second.second.cum_hipri_usage_bytes();
      wip.cum_lopri_usage_bytes += flow_time_info.second.second.cum_lopri_usage_bytes();
    }
  }

  for (auto& p : agg_wips_) {
    AggWIP& wip = p.second;
    absl::Time time = absl::UnixEpoch();
    if (wip.oldest_active_time != absl::InfiniteFuture()) {
      time = wip.oldest_active_time;
    } else if (wip.newest_dead_time != absl::InfinitePast()) {
      time = wip.newest_dead_time;
    } else {
      SPDLOG_LOGGER_ERROR(&logger_,
                          "AggWIP for {} has no oldest active or newest dead time",
                          wip.state.flow().ShortDebugString());
    }
    wip.state.UpdateUsage(
        {
            .time = time,
            .sum_child_usage_bps = wip.sum_ewma_usage_bps,
            .cum_hipri_usage_bytes = wip.cum_hipri_usage_bytes,
            .cum_lopri_usage_bytes = wip.cum_lopri_usage_bytes,
        },
        config_.usage_history_window, *agg_demand_predictor_);

    proto::AggInfo agg_info;
    *agg_info.mutable_parent() = wip.state.cur();
    *agg_info.mutable_children() = {wip.children.begin(), wip.children.end()};

    func(time, agg_info);
  }
}

FlowAggregator::AggWIP& FlowAggregator::GetAggWIP(const proto::FlowMarker& child) {
  proto::FlowMarker m = config_.get_agg_flow_fn(child);
  auto iter = agg_wips_.find(m);
  if (iter == agg_wips_.end()) {
    iter = agg_wips_.insert({m, AggWIP{.state = AggState(m)}}).first;
  }
  return iter->second;
}

}  // namespace heyp
