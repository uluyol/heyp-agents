#include "heyp/alg/downgrade/impl-hybrid-hashing.h"

#include "heyp/alg/fairness/nth-element.h"

namespace heyp {

template <FVSource vol_source>
std::vector<bool> HybridHashingDowngradeSelector<vol_source>::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger) {
  flow_volumes_and_id_.clear();
  flow_volumes_and_id_.reserve(agg_info.children().size());
  for (const proto::FlowInfo& child : agg_info.children()) {
    flow_volumes_and_id_.push_back(
        {GetFlowVolume(child, vol_source), child.flow().host_id()});
  }

  const size_t num_demand_aware =
      std::min<size_t>(num_demand_aware_, flow_volumes_and_id_.size());

  NthElement(flow_volumes_and_id_.begin(),
             flow_volumes_and_id_.begin() + num_demand_aware, flow_volumes_and_id_.end(),
             VolumeIdComparator());

  flow_volumes_and_id_.resize(num_demand_aware);

  int64_t lopri_interval = num_demand_aware + 1;  // no LOPRI at all
  if (want_frac_lopri > 0) {
    lopri_interval = std::round(1.0 / want_frac_lopri);
  }
}

template std::vector<bool>
HybridHashingDowngradeSelector<FVSource::kPredictedDemand>::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger);

template std::vector<bool>
HybridHashingDowngradeSelector<FVSource::kUsage>::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger);

}  // namespace heyp