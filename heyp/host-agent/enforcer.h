#ifndef HEYP_HOST_AGENT_ENFORCER_H_
#define HEYP_HOST_AGENT_ENFORCER_H_

#include "absl/types/span.h"
#include "heyp/host-agent/flow-tracker.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

class HostEnforcerInterface {
 public:
  ~HostEnforcerInterface() = default;

  virtual void EnforceAllocs(const FlowStateProvider& flow_state_provider,
                             const proto::HostAlloc& host_alloc) = 0;
};

class HostEnforcer : public HostEnforcerInterface {
 public:
  void EnforceAllocs(const FlowStateProvider& flow_state_provider,
                     const proto::HostAlloc& host_alloc) override;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_ENFORCER_H_
