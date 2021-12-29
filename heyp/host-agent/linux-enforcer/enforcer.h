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
#include "heyp/host-agent/linux-enforcer/iptables-controller.h"
#include "heyp/host-agent/linux-enforcer/tc-caller.h"
#include "heyp/host-agent/simulated-wan-db.h"
#include "heyp/io/debug-output-logger.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/heyp.pb.h"
#include "spdlog/spdlog.h"

namespace heyp {

struct MatchedHostFlows {
  using Vec = absl::InlinedVector<proto::FlowMarker, 4>;

  Vec hipri;
  Vec lopri;
};

using MatchHostFlowsFunc = std::function<MatchedHostFlows(
    const FlowStateProvider&, const proto::FlowAlloc&, spdlog::logger*)>;

// ExpandDestIntoHostsSinglePri matches all traffic in an FG to *either* HIPRI or LOPRI.
// It cannot be used with allocations that provide both HIPRI and LOPRI limits for a
// single FG.
//
// It uses the StaticDCMatcher to match any flow that could possibly belong to the FG.
MatchedHostFlows ExpandDestIntoHostsSinglePri(
    const StaticDCMapper* dc_mapper, const FlowStateProvider& flow_state_provider,
    const proto::FlowAlloc& flow_alloc, spdlog::logger* logger);

struct FlowNetemConfig {
  proto::FlowMarker flow;
  std::vector<proto::FlowMarker> matched_flows;
  SimulatedWanDB::QoSNetemConfig netem;
};

bool operator==(const FlowNetemConfig& lhs, const FlowNetemConfig& rhs);
std::ostream& operator<<(std::ostream& os, const FlowNetemConfig& c);

std::vector<FlowNetemConfig> AllNetemConfigs(const StaticDCMapper& dc_mapper,
                                             const SimulatedWanDB& simulated_wan,
                                             const std::string& my_dc,
                                             uint64_t my_host_id);

class LinuxHostEnforcer : public HostEnforcer {
 public:
  LinuxHostEnforcer(absl::string_view device,
                    const MatchHostFlowsFunc& match_host_flows_fn,
                    const proto::HostEnforcerConfig& config);

  LinuxHostEnforcer(absl::string_view device,
                    const MatchHostFlowsFunc& match_host_flows_fn,
                    const proto::HostEnforcerConfig& config,
                    std::unique_ptr<TcCallerIface> tc_caller);

  absl::Status ResetDeviceConfig();

  // InitSimulatedWan creates qdiscs and iptables rules to simulate a wide-area network
  // based on the provided netem configs. If all flows are provided now, EnforceAllocs
  // will not create additional qdiscs. If contains_all_flows is true, then EnforceAllocs
  // will report an error whenever it creates additional qdiscs.
  absl::Status InitSimulatedWan(std::vector<FlowNetemConfig> configs,
                                bool contains_all_flows);

  // HostEnforcer interface
  void EnforceAllocs(const FlowStateProvider& flow_state_provider,
                     const proto::AllocBundle& bundle) override;
  void LogState() override;
  IsLopriFunc GetIsLopriFunc() const override;

 private:
  struct FlowSys {
    struct Priority {
      std::string class_id;
      int64_t cur_rate_limit_bps = 0;
      bool did_create_class = false;
      bool update_after_ipt_change = false;
    };

    Priority hipri;
    Priority lopri;

    MatchedHostFlows matched;
  };

  struct StageTrafficControlForFlowArgs {
    int64_t rate_limit_bps;                                        // required
    const proto::NetemConfig* netem_config = nullptr;              // optional
    FlowSys::Priority* sys;                                        // required
    std::vector<FlowSys::Priority*>* classes_to_create = nullptr;  // optional
    int* create_count;                                             // required
    int* update_count;                                             // required
  };

  const proto::HostEnforcerConfig config_;
  const std::string device_;
  const MatchHostFlowsFunc match_host_flows_fn_;
  spdlog::logger logger_;
  TimedMutex mu_;
  absl::Cord tc_batch_input_ ABSL_GUARDED_BY(mu_);
  std::unique_ptr<TcCallerIface> tc_caller_ ABSL_GUARDED_BY(mu_);
  iptables::Controller ipt_controller_ ABSL_GUARDED_BY(mu_);
  DebugOutputLogger debug_logger_ ABSL_GUARDED_BY(mu_);
  int32_t next_class_id_ ABSL_GUARDED_BY(mu_);

  absl::flat_hash_map<proto::FlowMarker, std::unique_ptr<FlowSys>, HashFlowNoJob,
                      EqFlowNoJob>
      sys_info_ ABSL_GUARDED_BY(
          mu_);  // entries are never deleted, values are pointer for stability

  mutable absl::Mutex snapshot_mu_;
  iptables::SettingBatch ipt_snapshot_ ABSL_GUARDED_BY(snapshot_mu_);

  FlowSys* GetSysInfo(const proto::FlowMarker& flow) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  FlowSys* GetOrCreateSysInfo(const proto::FlowMarker& flow)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::Status ResetIptables() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  absl::Status ResetTrafficControl() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void StageTrafficControlForFlow(StageTrafficControlForFlowArgs args)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void StageIptablesForFlow(const MatchedHostFlows::Vec& matched_flows,
                            const std::string& dscp, const std::string& class_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_LINUX_ENFORCER_ENFORCER_H_
