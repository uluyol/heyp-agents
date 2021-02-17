#include "heyp/host-agent/enforcer.h"

#include "glog/logging.h"

namespace heyp {

void NopHostEnforcer::EnforceAllocs(const FlowStateProvider& flow_state_provider,
                                    const proto::AllocBundle& bundle) {
  LOG(INFO) << "NopHostEnforcer: got alloc to enforce (ignored)";
}

}  // namespace heyp
