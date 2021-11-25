#ifndef HEYP_ALG_FLOW_VOLUME_H_
#define HEYP_ALG_FLOW_VOLUME_H_

#include <cstdint>

#include "heyp/proto/heyp.pb.h"

namespace heyp {

enum class FVSource {
  kPredictedDemand,
  kUsage,
};

inline int64_t GetFlowVolume(const proto::FlowInfo& info, FVSource source) {
  if (source == FVSource::kPredictedDemand) {
    return info.predicted_demand_bps();
  }
  // source == FVSource::kUsage
  return info.ewma_usage_bps();
}

}  // namespace heyp

#endif  // HEYP_ALG_FLOW_VOLUME_H_
