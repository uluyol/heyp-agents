#include "heyp/host-agent/enforcer.h"

#include "heyp/log/spdlog.h"

namespace heyp {

NopHostEnforcer::NopHostEnforcer() : logger_(MakeLogger("nop-host-enforcer")) {}

void NopHostEnforcer::EnforceAllocs(const FlowStateProvider& flow_state_provider,
                                    const proto::AllocBundle& bundle) {
  SPDLOG_LOGGER_INFO(&logger_, "got alloc to enforce (ignored)");
}

void NopHostEnforcer::LogState() {
  SPDLOG_LOGGER_INFO(&logger_, "call to log state (ignored)");
}

IsLopriFunc NopHostEnforcer::GetIsLopriFunc() const {
  return [](const proto::FlowMarker& flow, spdlog::logger* logger) { return false; };
}

}  // namespace heyp
