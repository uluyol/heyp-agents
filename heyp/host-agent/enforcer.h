#ifndef HEYP_HOST_AGENT_ENFORCER_H_
#define HEYP_HOST_AGENT_ENFORCER_H_

#include <functional>

#include "absl/types/span.h"
#include "heyp/host-agent/flow-tracker.h"
#include "heyp/proto/heyp.pb.h"
#include "spdlog/spdlog.h"

namespace heyp {

using IsLopriFunc =
    std::function<bool(const proto::FlowMarker& flow, spdlog::logger* logger)>;

class HostEnforcer {
 public:
  virtual ~HostEnforcer() = default;

  virtual void EnforceAllocs(const FlowStateProvider& flow_state_provider,
                             const proto::AllocBundle& bundle) = 0;

  virtual IsLopriFunc GetIsLopriFunc() const = 0;
};

class NopHostEnforcer : public HostEnforcer {
 public:
  NopHostEnforcer();

  void EnforceAllocs(const FlowStateProvider& flow_state_provider,
                     const proto::AllocBundle& bundle) override;

  IsLopriFunc GetIsLopriFunc() const override;

 private:
  spdlog::logger logger_;
};

// See linux-enforcer/enforcer.h for an implementation of a real enforcer.

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_ENFORCER_H_
