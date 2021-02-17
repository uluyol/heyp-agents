#ifndef HEYP_HOST_AGENT_LINUX_ENFORCER_ENFORCER_H_
#define HEYP_HOST_AGENT_LINUX_ENFORCER_ENFORCER_H_

#include <functional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
#include "heyp/flows/dc-mapper.h"
#include "heyp/host-agent/enforcer.h"
#include "heyp/host-agent/linux-enforcer/iptables-controller.h"
#include "heyp/host-agent/linux-enforcer/tc-caller.h"
#include "heyp/proto/alg.h"
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
  LinuxHostEnforcer(absl::string_view device,
                    const MatchHostFlowsFunc& match_host_flows_fn);

  LinuxHostEnforcer(const LinuxHostEnforcer&) = delete;
  LinuxHostEnforcer& operator=(const LinuxHostEnforcer&) = delete;

  absl::Status ResetDeviceConfig();

  void EnforceAllocs(const FlowStateProvider& flow_state_provider,
                     const proto::AllocBundle& bundle) override;

 private:
  struct FlowSys {
    struct Priority {
      std::string class_id;
      int64_t cur_rate_limit_bps = 0;
      bool did_create_class = false;
      bool did_create_filter = false;
      bool update_after_ipt_change = false;
    };

    Priority hipri;
    Priority lopri;
  };

  absl::Status ResetIptables();
  absl::Status ResetTrafficControl();

  void StageIptablesForFlow(const MatchedHostFlows::Vec& matched_flows,
                            const std::string& dscp, const std::string& class_id);
  absl::Status UpdateTrafficControlForFlow(int64_t rate_limit_bps,
                                           FlowSys::Priority& sys);

  const std::string device_;
  const MatchHostFlowsFunc match_host_flows_fn_;
  TcCaller tc_caller_;
  iptables::Controller ipt_controller_;
  int32_t next_class_id_;

  absl::flat_hash_map<proto::FlowMarker, FlowSys, HashFlow, EqFlow>
      sys_info_;  // entries are never deleted
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_LINUX_ENFORCER_ENFORCER_H_
