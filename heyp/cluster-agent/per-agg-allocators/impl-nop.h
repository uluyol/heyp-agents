#ifndef HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_NOP_H_
#define HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_NOP_H_

#include "heyp/alg/debug.h"
#include "heyp/cluster-agent/per-agg-allocators/interface.h"
#include "heyp/log/spdlog.h"

namespace heyp {

class NopAllocator : public PerAggAllocator {
 public:
  NopAllocator() : logger_(MakeLogger("nop-alloc")) {}

  std::vector<proto::FlowAlloc> AllocAgg(
      absl::Time time, const proto::AggInfo& agg_info,
      proto::DebugAllocRecord::DebugState* debug_state) {
    const bool should_debug = DebugQosAndRateLimitSelection();
    if (should_debug) {
      if (should_debug) {
        SPDLOG_LOGGER_INFO(&logger_, "returning empty alloc for time = {}", time);
      }
    }
    return {};
  }

 private:
  spdlog::logger logger_;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_PER_AGG_ALLOCATORS_IMPL_NOP_H_
