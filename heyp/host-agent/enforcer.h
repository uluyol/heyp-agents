#ifndef HEYP_HOST_AGENT_ENFORCER_H_
#define HEYP_HOST_AGENT_ENFORCER_H_

#include "absl/types/span.h"
#include "heyp/host-agent/flow-tracker.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

class HostEnforcer {
 public:
  virtual ~HostEnforcer() = default;

  virtual void EnforceAllocs(const FlowStateProvider& flow_state_provider,
                             const proto::AllocBundle& bundle) = 0;
};

class NopHostEnforcer : public HostEnforcer {
 public:
  void EnforceAllocs(const FlowStateProvider& flow_state_provider,
                     const proto::AllocBundle& bundle) override;
};

// See linux-enforcer/enforcer.h for an implementation of a real enforcer.

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_ENFORCER_H_
