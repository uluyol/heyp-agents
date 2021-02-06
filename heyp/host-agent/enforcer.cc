#include "heyp/host-agent/enforcer.h"

#include "glog/logging.h"

namespace heyp {

// TODO: track state
// TODO: translate allocs to rules
// TODO: translate rules to commands
// TODO: commands to delete old rules

void HostEnforcer::EnforceAllocs(const FlowStateProvider& flow_state_provider,
                                 const proto::AllocBundle& bundle) {
  LOG(INFO) << "TODO: implement alloc enforcement";
}

}  // namespace heyp
