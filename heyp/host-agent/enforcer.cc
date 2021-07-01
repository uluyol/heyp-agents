#include "heyp/host-agent/enforcer.h"

#include "heyp/log/logging.h"

namespace heyp {

void NopHostEnforcer::EnforceAllocs(const FlowStateProvider& flow_state_provider,
                                    const proto::AllocBundle& bundle) {
  LOG(INFO) << "NopHostEnforcer: got alloc to enforce (ignored)";
}

bool NopHostEnforcer::IsLopri(const proto::FlowMarker& flow) { return false; }

}  // namespace heyp
