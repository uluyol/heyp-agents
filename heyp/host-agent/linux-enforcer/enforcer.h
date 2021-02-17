#ifndef HEYP_HOST_AGENT_LINUX_ENFORCER_ENFORCER_H_
#define HEYP_HOST_AGENT_LINUX_ENFORCER_ENFORCER_H_

#include <functional>
#include <string>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "heyp/flows/dc-mapper.h"
#include "heyp/host-agent/enforcer.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

struct MatchedHostFlows {
  using Vec = absl::InlinedVector<proto::FlowMarker, 4>;

  Vec hipri;
  Vec lopri;
};

using MatchHostFlowsFunc =
    std::function<MatchedHostFlows(const FlowStateProvider&, const proto::FlowAlloc&)>;

MatchedHostFlows ExpandDestIntoHostsSinglePri(
    const StaticDCMapper* dc_mapper, const FlowStateProvider& flow_state_provider,
    const proto::FlowAlloc& flow_alloc);

// TODO: track hipri/lopri
class LinuxHostEnforcer : public HostEnforcer {
 public:
  static std::unique_ptr<LinuxHostEnforcer> Create(
      absl::string_view device, const MatchHostFlowsFunc& match_host_flows_fn);

  virtual ~LinuxHostEnforcer() = default;

  virtual absl::Status ResetDeviceConfig() = 0;

  // Inherited from HostEnforcer
  // void EnforceAllocs(const FlowStateProvider& flow_state_provider,
  //                   const proto::AllocBundle& bundle) = 0;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_LINUX_ENFORCER_ENFORCER_H_
