#ifndef HEYP_FLOWS_AGGREGATOR_H_
#define HEYP_FLOWS_AGGREGATOR_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/time/time.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/flows/state.h"
#include "heyp/proto/alg.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

class FlowAggregator {
 public:
  struct Config {
    absl::Duration usage_history_window = absl::Seconds(120);
  };

  FlowAggregator(std::unique_ptr<DemandPredictor> agg_demand_predictor,
                 Config config);

  void Update(const proto::InfoBundle& bundle);
  void Remove(const proto::FlowMarker& bundler_marker);

  std::vector<proto::AggInfo> CollectSnapshot(absl::Time time);

 private:
  template <typename ValueType>
  using FlowMap =
      absl::flat_hash_map<proto::FlowMarker, ValueType, HashFlow, EqFlow>;

  struct AggState {
    FlowState state;
    int64_t cum_hipri_usage_bytes = 0;
    int64_t cum_lopri_usage_bytes = 0;

    // Reset and used only in CollectSnapshot.
    int64_t sum_ewma_usage_bps = 0;
    std::vector<FlowStateSnapshot> host_info;
  };

  struct ChildState {
    absl::Time last_updated = absl::InfinitePast();
    FlowMap<proto::FlowInfo> active;
    FlowMap<proto::FlowInfo> dead;
  };

  AggState& GetAggState(proto::FlowMarker flow_marker);

  const Config config_;
  const std::unique_ptr<DemandPredictor> agg_demand_predictor_;

  ClusterFGMap<AggState> agg_states_;
  absl::flat_hash_map<int64_t, HostState> host_states_;
};

}  // namespace heyp

#endif  // HEYP_FLOWS_AGGREGATOR_H_
