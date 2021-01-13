#ifndef HEYP_CLUSTER_AGENT_FG_TRACKER_H_
#define HEYP_CLUSTER_AGENT_FG_TRACKER_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/time/time.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/flows/cluster-fg-state.h"
#include "heyp/flows/state.h"
#include "heyp/proto/alg.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

template <typename ValueType>
using ClusterFGMap = absl::flat_hash_map<proto::FlowMarker, ValueType,
                                         HashClusterFlow, EqClusterFlow>;

class ClusterFGTracker {
 public:
  struct Config {
    absl::Duration host_usage_history_window = absl::Seconds(120);
    absl::Duration cluster_usage_history_window = absl::Seconds(120);
  };

  ClusterFGTracker(std::unique_ptr<DemandPredictor> cluster_demand_predictor,
                   std::unique_ptr<DemandPredictor> host_demand_predictor,
                   Config config);

  void UpdateHost(const proto::HostInfo& host_info);
  void RemoveHost(int64_t host_id);

  std::vector<ClusterFGState> CollectSnapshot(absl::Time time);

 private:
  struct AggState {
    FlowState state;
    int64_t cum_hipri_usage_bytes = 0;
    int64_t cum_lopri_usage_bytes = 0;

    // Reset and used only in CollectSnapshot.
    int64_t sum_ewma_usage_bps = 0;
    std::vector<FlowState> host_info;
  };

  struct HostAggState {
    FlowState state;
    int64_t cum_hipri_usage_bytes = 0;
    int64_t cum_lopri_usage_bytes = 0;
    int gen = 0;  // used to garbage collect old versions
  };

  struct HostState {
    ClusterFGMap<HostAggState> agg_states;
    int gen = 1;
  };

  AggState& GetAggState(proto::FlowMarker flow_marker);

  const Config config_;
  const std::unique_ptr<DemandPredictor> cluster_demand_predictor_;
  const std::unique_ptr<DemandPredictor> host_demand_predictor_;

  ClusterFGMap<AggState> agg_states_;
  absl::flat_hash_map<int64_t, HostState> host_states_;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_FG_TRACKER_H_
