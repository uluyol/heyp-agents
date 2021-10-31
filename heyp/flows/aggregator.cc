#include "heyp/flows/aggregator.h"

#include <algorithm>

#include "absl/strings/str_join.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/alg.h"
#include "heyp/proto/constructors.h"
#include "heyp/threads/mutex-helpers.h"

namespace heyp {
namespace {

CompareFlowOptions HostFlowOptions() {
  return CompareFlowOptions{
      .cmp_fg = true,
      .cmp_job = true,
      .cmp_src_host = true,
      .cmp_host_flow = false,
      .cmp_seqnum = false,
  };
}

CompareFlowOptions ClusterFlowOptions() {
  return CompareFlowOptions{
      .cmp_fg = true,
      .cmp_job = false,
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
            h.clear_job();
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

ParID FlowAggregator::GetBundlerID(const proto::FlowMarker& bundler) {
  return bundle_states_.GetID(bundler);
}

void FlowAggregator::Update(ParID bundler_id, const proto::InfoBundle& bundle) {
  const absl::Time timestamp = FromProtoTimestamp(bundle.timestamp());

  bundle_states_.OnID(bundler_id, [&](BundleState& bs) {
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
  });
}

constexpr bool kDebugSpikes = false;

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

  // Get a pointer to agg_wips_ here, while we have the lock since clang's thread-safety
  // analysis can't read into callbacks.
  // We're still holding the lock when we execute the code in ForEach.
  FlowMap<AggWIP>* agg_wips = &agg_wips_;
  bundle_states_.ForEach(
      0, bundle_states_.NumIDs(), [&](ParID bundler_id, BundleState& bs) {
        for (const auto& flow_time_info : bs.active) {
          AggWIP* wip = GetAggWIP(config_, flow_time_info.first, agg_wips);
          wip->oldest_active_time =
              std::min(wip->oldest_active_time, flow_time_info.second.first);
          wip->cum_hipri_usage_bytes +=
              flow_time_info.second.second.cum_hipri_usage_bytes();
          wip->cum_lopri_usage_bytes +=
              flow_time_info.second.second.cum_lopri_usage_bytes();
          wip->sum_ewma_usage_bps += flow_time_info.second.second.ewma_usage_bps();
          wip->children.push_back(flow_time_info.second.second);
        }

        for (const auto& flow_time_info : bs.dead) {
          AggWIP* wip = GetAggWIP(config_, flow_time_info.first, agg_wips);
          wip->newest_dead_time =
              std::max(wip->newest_dead_time, flow_time_info.second.first);
          wip->cum_hipri_usage_bytes +=
              flow_time_info.second.second.cum_hipri_usage_bytes();
          wip->cum_lopri_usage_bytes +=
              flow_time_info.second.second.cum_lopri_usage_bytes();
        }
      });

  auto bundle_state_to_string = [this](
                                    BundleStatesMap& states,
                                    const proto::FlowMarker& wanted_agg) -> std::string {
    std::vector<std::string> lines;
    lines.push_back("active: [");
    ParID num_ids = states.NumIDs();
    states.ForEach(0, num_ids, [&](ParID bundler_id, BundleState& bs) {
      for (auto& flow_time_info : bs.active) {
        if (IsSameFlow(config_.get_agg_flow_fn(flow_time_info.second.second.flow()),
                       wanted_agg)) {
          lines.push_back("\t" + flow_time_info.second.second.ShortDebugString());
        }
      }
    });
    lines.push_back("]");
    lines.push_back("dead: [");
    states.ForEach(0, num_ids, [&](ParID bundler_id, BundleState& bs) {
      for (auto& flow_time_info : bs.dead) {
        if (IsSameFlow(config_.get_agg_flow_fn(flow_time_info.second.second.flow()),
                       wanted_agg)) {
          lines.push_back("\t" + flow_time_info.second.second.ShortDebugString());
        }
      }
    });
    lines.push_back("]");
    return absl::StrJoin(lines, "\n");
  };

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

    if (kDebugSpikes && prev_agg_wips_.find(p.first) != prev_agg_wips_.end()) {
      const AggWIP& last = prev_agg_wips_.at(p.first);
      if (last.sum_ewma_usage_bps * 1.1 < wip.sum_ewma_usage_bps ||
          (wip.cum_hipri_usage_bytes + wip.cum_lopri_usage_bytes) >
              ((3 << 30) / 8 +
               (last.cum_hipri_usage_bytes + last.cum_lopri_usage_bytes))) {
        SPDLOG_LOGGER_INFO(&logger_,
                           "flow agg {} usage increased by {}%: from {} to {}; "
                           "cum_usage_bytes from {} to {}\nold "
                           "bundle states:{}\ncur bundle states:{}",
                           p.first.ShortDebugString(),
                           100 * static_cast<double>(wip.sum_ewma_usage_bps) /
                               static_cast<double>(last.sum_ewma_usage_bps),
                           last.sum_ewma_usage_bps, wip.sum_ewma_usage_bps,
                           last.cum_hipri_usage_bytes + last.cum_lopri_usage_bytes,
                           wip.cum_hipri_usage_bytes + wip.cum_lopri_usage_bytes,
                           bundle_state_to_string(*prev_bundle_states_, p.first),
                           bundle_state_to_string(bundle_states_, p.first));
      }
    }
  }

  if (kDebugSpikes) {
    prev_agg_wips_ = agg_wips_;
    prev_bundle_states_ = bundle_states_.BestEffortCopy();
  }
}

FlowAggregator::AggWIP* FlowAggregator::GetAggWIP(const Config& config,
                                                  const proto::FlowMarker& child,
                                                  FlowMap<AggWIP>* wips) {
  proto::FlowMarker m = config.get_agg_flow_fn(child);
  auto iter = wips->find(m);
  if (iter == wips->end()) {
    iter = wips->insert({m, AggWIP{.state = AggState(m, false)}}).first;
  }
  return &iter->second;
}

}  // namespace heyp
