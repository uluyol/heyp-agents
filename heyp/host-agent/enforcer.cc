#include "heyp/host-agent/enforcer.h"

#include "glog/logging.h"

namespace heyp {

void HostEnforcer::EnforceAllocs(const FlowTracker& flow_tracker,
                                 absl::Span<proto::FlowAlloc> flow_allocs) {
  LOG(INFO) << "TODO: implement alloc enforcement";
}

}  // namespace heyp
