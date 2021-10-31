#ifndef HEYP_FLOWS_AGGREGATOR_H_
#define HEYP_FLOWS_AGGREGATOR_H_

#include <mutex>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/flows/state.h"
#include "heyp/proto/alg.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/threads/mutex-helpers.h"
#include "heyp/threads/par-indexed-map.h"
#include "spdlog/spdlog.h"

namespace heyp {

class FlowAggregator;

std::unique_ptr<FlowAggregator> NewConnToHostAggregator(
    std::unique_ptr<DemandPredictor> host_demand_predictor,
    absl::Duration usage_history_window);

std::unique_ptr<FlowAggregator> NewHostToClusterAggregator(
    std::unique_ptr<DemandPredictor> cluster_demand_predictor,
    absl::Duration usage_history_window);

class FlowAggregator {
 public:
  struct Config {
    absl::Duration usage_history_window = absl::Seconds(120);
    std::function<proto::FlowMarker(const proto::FlowMarker&)> get_agg_flow_fn;

    std::function<bool(proto::FlowMarker)> is_valid_parent;  // optional
    std::function<bool(proto::FlowMarker)> is_valid_child;   // optional
  };

  FlowAggregator(std::unique_ptr<DemandPredictor> agg_demand_predictor, Config config);

  // Update the stats for a 'bundle' of flows at once.
  //
  // The bundler is expected to be permanently responsible for the provided
  // flows (i.e. the same flow should only ever be reported by one bundler).
  void Update(ParID bundler_id, const proto::InfoBundle& bundle);

  ParID GetBundlerID(const proto::FlowMarker& bundler);

  void ForEachAgg(absl::FunctionRef<void(absl::Time, const proto::AggInfo&)> func);

 private:
  struct AggWIP {
    // Updated in ForEachAgg but needs to persist to track historical usage.
    AggState state;

    // Reset and used only in ForEachAgg.
    absl::Time oldest_active_time = absl::InfiniteFuture();
    absl::Time newest_dead_time = absl::InfinitePast();
    int64_t cum_hipri_usage_bytes = 0;
    int64_t cum_lopri_usage_bytes = 0;
    int64_t sum_ewma_usage_bps = 0;
    std::vector<proto::FlowInfo> children;
  };

  template <typename ValueType>
  using FlowMap = absl::flat_hash_map<proto::FlowMarker, ValueType, HashFlow, EqFlow>;

  struct BundleState {
    absl::Time last_updated = absl::InfinitePast();
    FlowMap<std::pair<absl::Time, proto::FlowInfo>> active;
    FlowMap<std::pair<absl::Time, proto::FlowInfo>> dead;
  };
  using BundleStatesMap = ParIndexedMap<proto::FlowMarker, BundleState, FlowMap<ParID>>;

  static AggWIP* GetAggWIP(const Config& config, const proto::FlowMarker& child,
                           FlowMap<AggWIP>* wips);

  const Config config_;
  const std::unique_ptr<DemandPredictor> agg_demand_predictor_;
  spdlog::logger logger_;

  TimedMutex mu_;
  FlowMap<AggWIP> agg_wips_ ABSL_GUARDED_BY(mu_);
  BundleStatesMap bundle_states_;
  // For debugging
  FlowMap<AggWIP> prev_agg_wips_ ABSL_GUARDED_BY(mu_);
  std::unique_ptr<BundleStatesMap> prev_bundle_states_ ABSL_GUARDED_BY(mu_);
};

}  // namespace heyp

#endif  // HEYP_FLOWS_AGGREGATOR_H_
