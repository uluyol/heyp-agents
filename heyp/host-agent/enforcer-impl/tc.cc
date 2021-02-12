#include "heyp/host-agent/enforcer-impl/tc.h"

#include "glog/logging.h"

namespace heyp {

TcHostEnforcer::TcHostEnforcer(absl::string_view device,
                               const StaticDCMapper &dc_mapper)
    : device_(device), dc_mapper_(dc_mapper) {}

void TcHostEnforcer::EnforceAllocs(const FlowStateProvider &flow_state_provider,
                                   const proto::AllocBundle &bundle) {
  LOG(FATAL) << "TODO: implement";
}

}  // namespace heyp