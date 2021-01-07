#include "heyp/host-agent/enforcer.h"

#include "glog/logging.h"

namespace heyp {

void HostEnforcer::EnforceAllocs(const FlowStateProvider& flow_state_provider,
                                 const proto::HostAlloc& host_alloc) {
  LOG(INFO) << "TODO: implement alloc enforcement";
}

}  // namespace heyp
