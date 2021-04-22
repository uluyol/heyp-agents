#ifndef HEYP_HOST_AGENT_LINUX_ENFORCER_ENFORCER_H_
#define HEYP_HOST_AGENT_LINUX_ENFORCER_ENFORCER_H_

#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "heyp/flows/dc-mapper.h"
#include "heyp/host-agent/enforcer.h"
#include "heyp/host-agent/simulated-wan-db.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

struct MatchedHostFlows {
  using Vec = absl::InlinedVector<proto::FlowMarker, 4>;

  Vec hipri;
  Vec lopri;
};

using MatchHostFlowsFunc =
    std::function<MatchedHostFlows(const FlowStateProvider&, const proto::FlowAlloc&)>;

// ExpandDestIntoHostsSinglePri matches all traffic in an FG to *either* HIPRI or LOPRI.
// It cannot be used with allocations that provide both HIPRI and LOPRI limits for a
// single FG.
//
// It uses the StaticDCMatcher to match any flow that could possibly belong to the FG.
MatchedHostFlows ExpandDestIntoHostsSinglePri(
    const StaticDCMapper* dc_mapper, const FlowStateProvider& flow_state_provider,
    const proto::FlowAlloc& flow_alloc);

struct FlowNetemConfig {
  proto::FlowMarker flow;
  std::vector<proto::FlowMarker> matched_flows;
  proto::NetemConfig netem;
};

bool operator==(const FlowNetemConfig& lhs, const FlowNetemConfig& rhs);
std::ostream& operator<<(std::ostream& os, const FlowNetemConfig& c);

std::vector<FlowNetemConfig> AllNetemConfigs(const StaticDCMapper& dc_mapper,
                                             const SimulatedWanDB& simulated_wan,
                                             const std::string& my_dc,
                                             uint64_t my_host_id);

class LinuxHostEnforcer : public HostEnforcer {
 public:
  static std::unique_ptr<LinuxHostEnforcer> Create(
      absl::string_view device, const MatchHostFlowsFunc& match_host_flows_fn,
      absl::string_view debug_log_outdir = "");

  virtual ~LinuxHostEnforcer() = default;

  virtual absl::Status ResetDeviceConfig() = 0;

  // InitSimulatedWan creates qdiscs and iptables rules to simulate a wide-area network
  // based on the provided netem configs. If all flows are provided now, EnforceAllocs
  // will not create additional qdiscs. If contains_all_flows is true, then EnforceAllocs
  // will report an error whenever it creates additional qdiscs.
  virtual absl::Status InitSimulatedWan(std::vector<FlowNetemConfig> configs,
                                        bool contains_all_flows) = 0;

  // Inherited from HostEnforcer
  //
  // virtual bool IsLopri(const proto::FlowMarker& flow) = 0;
  //
  // void EnforceAllocs(const FlowStateProvider& flow_state_provider,
  //                   const proto::AllocBundle& bundle) = 0;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_LINUX_ENFORCER_ENFORCER_H_
