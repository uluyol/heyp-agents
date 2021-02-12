#ifndef HEYP_HOST_AGENT_ENFORCER_IMPL_TC_H_
#define HEYP_HOST_AGENT_ENFORCER_IMPL_TC_H_

#include <string>

#include "absl/strings/string_view.h"
#include "heyp/flows/dc-mapper.h"
#include "heyp/host-agent/enforcer-impl/tc-caller.h"
#include "heyp/host-agent/enforcer.h"

namespace heyp {

//
//
class TcHostEnforcer : public HostEnforcer {
 public:
  TcHostEnforcer(absl::string_view device, const StaticDCMapper& dc_mapper);

  absl::Status Reset();

  void EnforceAllocs(const FlowStateProvider& flow_state_provider,
                     const proto::AllocBundle& bundle) override;

 private:
  const std::string device_;
  const StaticDCMapper& dc_mapper_;
  TcCaller tc_caller_;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_ENFORCER_IMPL_TC_H_
