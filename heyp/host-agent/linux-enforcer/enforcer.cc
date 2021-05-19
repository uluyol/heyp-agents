#include "heyp/host-agent/linux-enforcer/enforcer.h"

#include <algorithm>
#include <limits>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "glog/logging.h"
#include "google/protobuf/util/message_differencer.h"
#include "heyp/host-agent/linux-enforcer/iptables-controller.h"
#include "heyp/host-agent/linux-enforcer/iptables.h"
#include "heyp/host-agent/linux-enforcer/tc-caller.h"
#include "heyp/io/debug-output-logger.h"
#include "heyp/proto/alg.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

MatchedHostFlows ExpandDestIntoHostsSinglePri(
    const StaticDCMapper* dc_mapper, const FlowStateProvider& flow_state_provider,
    const proto::FlowAlloc& flow_alloc) {
  MatchedHostFlows matched;
  MatchedHostFlows::Vec* expanded = &matched.hipri;
  if (flow_alloc.lopri_rate_limit_bps() > 0) {
    CHECK_EQ(flow_alloc.hipri_rate_limit_bps(), 0)
        << "ExpandDestIntoHostsSinglePri cannot accept both positive hipri and lopri "
           "rate limits";
    expanded = &matched.lopri;
  }
  auto& flow = flow_alloc.flow();
  if (flow.dst_addr().empty()) {
    auto hosts_ptr = dc_mapper->HostsForDC(flow.dst_dc());
    if (hosts_ptr == nullptr) {
      LOG(ERROR) << "no hosts match DC \"" << flow.dst_dc() << "\"";
    } else {
      expanded->reserve(hosts_ptr->size());
      for (const std::string& host : *hosts_ptr) {
        proto::FlowMarker f = flow;
        f.set_dst_addr(host);
        expanded->push_back(std::move(f));
      }
    }
  } else {
    expanded->push_back(flow);
  }
  return matched;
}

bool operator==(const FlowNetemConfig& lhs, const FlowNetemConfig& rhs) {
  if (!IsSameFlow(lhs.flow, rhs.flow)) {
    return false;
  }
  if (!google::protobuf::util::MessageDifferencer::Equivalent(lhs.netem, rhs.netem)) {
    return false;
  }
  if (lhs.matched_flows.size() != rhs.matched_flows.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.matched_flows.size(); ++i) {
    if (!IsSameFlow(lhs.matched_flows[i], rhs.matched_flows[i])) {
      return false;
    }
  }
  return true;
}

template <typename Proto>
struct ShortDebugFormatter {
  void operator()(std::string* out, const Proto& mesg) const {
    out->append(mesg.ShortDebugString());
  }
};

std::ostream& operator<<(std::ostream& os, const FlowNetemConfig& c) {
  return os << "FlowNetemConfig{\n"
            << "  flow = " << c.flow.ShortDebugString() << "\n  matched_flows = [\n    "
            << absl::StrJoin(c.matched_flows, "\n    ",
                             ShortDebugFormatter<proto::FlowMarker>())
            << "\n  ]\n  netem = " << c.netem.ShortDebugString() << "\n}";
}

std::vector<FlowNetemConfig> AllNetemConfigs(const StaticDCMapper& dc_mapper,
                                             const SimulatedWanDB& simulated_wan,
                                             const std::string& my_dc,
                                             uint64_t my_host_id) {
  std::vector<FlowNetemConfig> configs;
  for (const std::string& dst : dc_mapper.AllDCs()) {
    if (dst == my_dc) {
      continue;
    }
    const proto::NetemConfig* maybe_config = simulated_wan.GetNetem(my_dc, dst);
    if (maybe_config == nullptr) {
      continue;
    }
    FlowNetemConfig c;
    c.netem = *maybe_config;
    c.flow.set_src_dc(my_dc);
    c.flow.set_dst_dc(dst);
    c.flow.set_host_id(my_host_id);

    for (const std::string& host : *dc_mapper.HostsForDC(dst)) {
      c.matched_flows.push_back(c.flow);
      c.matched_flows.back().set_dst_addr(host);
    }
    configs.push_back(c);
  }
  return configs;
}

namespace {

uint16_t AssertValidPort(int32_t port32) {
  const int32_t kMaxPortNum = std::numeric_limits<uint16_t>::max();

  CHECK(port32 >= 0);
  CHECK(port32 <= kMaxPortNum);

  return static_cast<uint16_t>(port32);
}

// Implementation is separated from interface to reduce #includes in the header file and
// speed up compilation.
class LinuxHostEnforcerImpl : public LinuxHostEnforcer {
 public:
  LinuxHostEnforcerImpl(absl::string_view device,
                        const MatchHostFlowsFunc& match_host_flows_fn,
                        absl::string_view debug_log_outdir);

  absl::Status ResetDeviceConfig() override;

  absl::Status InitSimulatedWan(std::vector<FlowNetemConfig> configs,
                                bool contains_all_flows) override;

  void EnforceAllocs(const FlowStateProvider& flow_state_provider,
                     const proto::AllocBundle& bundle) override;

  bool IsLopri(const proto::FlowMarker& flow) override;

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

  absl::Status ResetIptables();
  absl::Status ResetTrafficControl();

  struct StageTrafficControlForFlowArgs {
    int64_t rate_limit_bps;                                        // required
    const proto::NetemConfig* netem_config = nullptr;              // optional
    FlowSys::Priority* sys;                                        // required
    std::vector<FlowSys::Priority*>* classes_to_create = nullptr;  // optional
    int* create_count;                                             // required
    int* update_count;                                             // required
  };

  void StageTrafficControlForFlow(StageTrafficControlForFlowArgs args);
  void StageIptablesForFlow(const MatchedHostFlows::Vec& matched_flows,
                            const std::string& dscp, const std::string& class_id);

  const std::string device_;
  const MatchHostFlowsFunc match_host_flows_fn_;
  absl::Cord tc_batch_input_;
  TcCaller tc_caller_;
  iptables::Controller ipt_controller_;
  DebugOutputLogger debug_logger_;
  int32_t next_class_id_;
  bool report_error_on_dyn_qdisc_;

  absl::flat_hash_map<proto::FlowMarker, std::unique_ptr<FlowSys>, HashFlow, EqFlow>
      sys_info_;  // entries are never deleted, values are pointer for stability
};

LinuxHostEnforcerImpl::LinuxHostEnforcerImpl(
    absl::string_view device, const MatchHostFlowsFunc& match_host_flows_fn,
    absl::string_view debug_log_outdir)
    : device_(device),
      match_host_flows_fn_(match_host_flows_fn),
      ipt_controller_(device),
      debug_logger_(debug_log_outdir),
      next_class_id_(2),
      report_error_on_dyn_qdisc_(false) {}

absl::Status LinuxHostEnforcerImpl::ResetDeviceConfig() {
  auto st = ResetTrafficControl();
  if (!st.ok()) {
    return absl::Status(st.code(),
                        absl::StrCat("failed to reset traffic control: ", st.message()));
  }
  st = ResetIptables();
  if (!st.ok()) {
    return absl::Status(st.code(),
                        absl::StrCat("failed to reset iptables: ", st.message()));
  }
  return absl::OkStatus();
}

constexpr int64_t kMaxBandwidthBps = 100 * (static_cast<int64_t>(1) << 30);  // 100 Gbps

constexpr char kDscpHipri[] = "AF21";
constexpr char kDscpLopri[] = "BE";

absl::Status LinuxHostEnforcerImpl::InitSimulatedWan(std::vector<FlowNetemConfig> configs,
                                                     bool contains_all_flows) {
  int num_created_classes = 0;
  int num_updated_classes = 0;

  std::vector<std::string> errors;
  tc_batch_input_.Clear();
  for (const FlowNetemConfig& c : configs) {
    if (sys_info_[c.flow] != nullptr) {
      errors.push_back(
          absl::StrCat("FlowSys already exists for flow: ", c.flow.ShortDebugString()));
      continue;
    }
    sys_info_[c.flow] = absl::make_unique<FlowSys>();
    FlowSys* sys = sys_info_[c.flow].get();
    StageTrafficControlForFlow({
        .rate_limit_bps = kMaxBandwidthBps,
        .netem_config = &c.netem,
        .sys = &sys->hipri,
        .create_count = &num_created_classes,
        .update_count = &num_updated_classes,
    });
    StageTrafficControlForFlow({
        .rate_limit_bps = kMaxBandwidthBps,
        .netem_config = &c.netem,
        .sys = &sys->lopri,
        .create_count = &num_created_classes,
        .update_count = &num_updated_classes,
    });
  }

  LOG(INFO) << absl::StrFormat("creating %d rate limiters and netem qdiscs",
                               num_created_classes);
  if (num_updated_classes > 0) {
    LOG(ERROR) << __func__ << ": impossible state: updating rate limiters";
  }
  absl::Status st = tc_caller_.Batch(tc_batch_input_, /*force=*/true);

  if (!st.ok()) {
    if (errors.empty()) {
      return absl::Status(
          st.code(),
          absl::StrCat("failed to create htb classes or qdiscs: ", st.message()));
    } else {
      errors.push_back(
          absl::StrCat("failed to create htb classes or qdiscs: ", st.message()));
      return absl::Status(
          st.code(), absl::StrCat("multiple errors:\n\t", absl::StrJoin(errors, "\n\t")));
    }
  }

  report_error_on_dyn_qdisc_ = contains_all_flows;

  // ==== Stage 2: set up iptables ====

  for (const FlowNetemConfig& c : configs) {
    FlowSys* sys = sys_info_[c.flow].get();
    sys->hipri.did_create_class = true;
    sys->lopri.did_create_class = true;

    sys->matched.hipri = {c.matched_flows.begin(), c.matched_flows.end()};
    StageIptablesForFlow(sys->matched.hipri, kDscpHipri, sys->hipri.class_id);
  }

  st = ipt_controller_.CommitChanges();

  if (!st.ok()) {
    if (errors.empty()) {
      return absl::Status(
          st.code(), absl::StrCat("failed to commit iptables config: ", st.message()));
    } else {
      errors.push_back(absl::StrCat("commit iptables config: ", st.message()));
      return absl::Status(
          st.code(), absl::StrCat("multiple errors:\n\t", absl::StrJoin(errors, "\n\t")));
    }
  }

  if (!errors.empty()) {
    if (errors.size() == 1) {
      return absl::FailedPreconditionError(errors[0]);
    } else {
      return absl::FailedPreconditionError(
          absl::StrCat("multiple errors:\n\t", absl::StrJoin(errors, "\n\t")));
    }
  }
  return absl::OkStatus();
}

absl::Status LinuxHostEnforcerImpl::ResetIptables() { return ipt_controller_.Clear(); }

absl::Status LinuxHostEnforcerImpl::ResetTrafficControl() {
  return tc_caller_.Batch(
      absl::Cord(absl::StrFormat("qdisc delete dev %s root\n"
                                 "qdisc add dev %s root handle 1: htb default 0",
                                 device_, device_)),
      /*force=*/true);
}

void LinuxHostEnforcerImpl::StageIptablesForFlow(
    const MatchedHostFlows::Vec& matched_flows, const std::string& dscp,
    const std::string& class_id) {
  if (matched_flows.empty()) {
    return;
  }

  CHECK(!class_id.empty()) << "class_id must be set; call UpdateTrafficControlForFlow "
                              "before StageIptablesForFlow";

  for (auto f : matched_flows) {
    ipt_controller_.Stage({
        .src_port = AssertValidPort(f.src_port()),
        .dst_port = AssertValidPort(f.dst_port()),
        .dst_addr = f.dst_addr(),
        .class_id = class_id,
        .dscp = dscp,
    });
  }
}

absl::string_view NetemDistToString(proto::NetemDelayDist dist) {
  switch (dist) {
    case proto::NETEM_NORMAL:
      return "normal";
    case proto::NETEM_UNIFORM:
      return "uniform";
    case proto::NETEM_PARETO:
      return "pareto";
    case proto::NETEM_PARETONORMAL:
      return "paretonormal";
  }
  return "UNKNOWN_NETEM_DIST";
}

void LinuxHostEnforcerImpl::StageTrafficControlForFlow(
    StageTrafficControlForFlowArgs args) {
  double rate_limit_mbps = args.rate_limit_bps;
  rate_limit_mbps /= 1024.0 * 1024.0;

  if (args.sys->class_id.empty()) {
    args.sys->class_id = absl::StrCat("1:", next_class_id_++);
  }

  if (!args.sys->did_create_class) {
    tc_batch_input_.Append(
        absl::StrFormat("class add dev %s parent 1: classid %s htb rate %fmbit\n",
                        device_, args.sys->class_id, rate_limit_mbps));
    if (args.netem_config != nullptr) {
      std::string netem_handle =
          absl::StrCat(absl::StripPrefix(args.sys->class_id, "1:"), ":0");
      tc_batch_input_.Append(absl::StrFormat(
          "qdisc add dev %s parent %s handle %s netem delay %dms %dms %f%% distribution "
          "%s\n",
          device_, args.sys->class_id, netem_handle, args.netem_config->delay_ms(),
          args.netem_config->delay_jitter_ms(),
          args.netem_config->delay_correlation_pct(),
          NetemDistToString(args.netem_config->delay_dist())));
    }
    if (args.classes_to_create != nullptr) {
      args.classes_to_create->push_back(args.sys);
    }
    (*args.create_count)++;
  } else {
    tc_batch_input_.Append(
        absl::StrFormat("class change dev %s parent 1: classid %s htb rate %fmbit\n",
                        device_, args.sys->class_id, rate_limit_mbps));
    (*args.update_count)++;
  }
}

// EnforceAllocs adjusts the rate limits and QoS for host traffic in 3 stages:
//
// 1. Create rate limiters for used (FG, QoS) pairs that do not have one and increase
// rate
//    limits for appropriate (FG, QoS) pairs.
//
// 2. Update iptables to direct flows into correct rate limiters and mark correct QoS.
//
// 3. Reduce rate limits for appropriate (FG, QoS) pairs.
//
void LinuxHostEnforcerImpl::EnforceAllocs(const FlowStateProvider& flow_state_provider,
                                          const proto::AllocBundle& bundle) {
  auto cleanup = absl::MakeCleanup([this] {
    if (debug_logger_.should_log()) {
      absl::Cord mangle_table;
      ipt_controller_.GetRunner()
          .SaveInto(iptables::Table::kMangle, mangle_table)
          .IgnoreError();
      bool have_qdisc_output = false;
      absl::Cord qdisc_output;
      if (tc_caller_.Call({"qdisc"}, false).ok()) {
        qdisc_output.Append(tc_caller_.RawOut());
        have_qdisc_output = true;
      }
      bool have_class_output = false;
      absl::Cord class_output;
      if (tc_caller_.Call({"class", "show", "dev", device_}, false).ok()) {
        class_output.Append(tc_caller_.RawOut());
        have_class_output = true;
      }

      absl::Time timestamp = absl::Now();
      debug_logger_.Write("iptables:mangle", mangle_table, timestamp);
      if (have_qdisc_output) {
        debug_logger_.Write("tc:qdisc", qdisc_output, timestamp);
      }
      if (have_class_output) {
        debug_logger_.Write("tc:class", class_output, timestamp);
      }
    }
  });

  // ==== Stage 1: Initialize qdiscs and increase any rate limits ====

  std::vector<FlowSys::Priority*> classes_to_create;
  tc_batch_input_.Clear();
  int create_count = 0;
  int update_count = 0;
  for (const proto::FlowAlloc& flow_alloc : bundle.flow_allocs()) {
    if (sys_info_[flow_alloc.flow()] == nullptr) {
      sys_info_[flow_alloc.flow()] = absl::make_unique<FlowSys>();
    }
    FlowSys* sys = sys_info_[flow_alloc.flow()].get();
    sys->matched = match_host_flows_fn_(flow_state_provider, flow_alloc);

    // Early update for traffic control (HIPRI)
    bool must_create = sys->hipri.class_id.empty() && !sys->matched.hipri.empty();
    if (must_create ||
        flow_alloc.hipri_rate_limit_bps() > sys->hipri.cur_rate_limit_bps) {
      StageTrafficControlForFlow({
          .rate_limit_bps = flow_alloc.hipri_rate_limit_bps(),
          .sys = &sys->hipri,
          .classes_to_create = &classes_to_create,
          .create_count = &create_count,
          .update_count = &update_count,
      });
      sys->hipri.update_after_ipt_change = false;
    } else if (flow_alloc.hipri_rate_limit_bps() < sys->hipri.cur_rate_limit_bps) {
      sys->hipri.update_after_ipt_change = true;
    }
    sys->hipri.cur_rate_limit_bps = flow_alloc.hipri_rate_limit_bps();

    // Early update for traffic control (LOPRI)
    must_create = sys->lopri.class_id.empty() && !sys->matched.lopri.empty();
    if (must_create ||
        flow_alloc.lopri_rate_limit_bps() > sys->lopri.cur_rate_limit_bps) {
      StageTrafficControlForFlow({
          .rate_limit_bps = flow_alloc.lopri_rate_limit_bps(),
          .sys = &sys->lopri,
          .classes_to_create = &classes_to_create,
          .create_count = &create_count,
          .update_count = &update_count,
      });
      sys->lopri.update_after_ipt_change = false;
    } else if (flow_alloc.lopri_rate_limit_bps() < sys->lopri.cur_rate_limit_bps) {
      sys->lopri.update_after_ipt_change = true;
    }
    sys->lopri.cur_rate_limit_bps = flow_alloc.lopri_rate_limit_bps();
  }

  LOG(INFO) << absl::StrFormat("creating %d rate limiters and increasing %d rate limits",
                               create_count, update_count);
  absl::Status st = tc_caller_.Batch(tc_batch_input_, /*force=*/true);
  if (!st.ok()) {
    LOG(ERROR) << "failed to init or increase rate limits for some flows: " << st;
    // Find out which classes have not been created
    st = tc_caller_.Call({"class", "show", "dev", device_}, false);

    if (st.ok()) {
      std::vector<std::string> found_classes;
      for (auto line : absl::StrSplit(tc_caller_.RawOut(), '\n')) {
        std::vector<absl::string_view> fields =
            absl::StrSplit(line, absl::ByAnyChar(" \t"));
        if (fields.size() >= 3) {
          found_classes.push_back(std::string(fields[2]));
        }
      }

      std::sort(found_classes.begin(), found_classes.end());
      for (FlowSys::Priority* sys : classes_to_create) {
        if (std::binary_search(found_classes.begin(), found_classes.end(),
                               sys->class_id)) {
          sys->did_create_class = true;
        } else {
          // failed to create class, will report error when staging iptables changes
        }
      }
    }
  } else {
    for (FlowSys::Priority* sys : classes_to_create) {
      sys->did_create_class = true;
    }
  }

  // ==== Stage 2: Update iptables ====

  for (const proto::FlowAlloc& flow_alloc : bundle.flow_allocs()) {
    FlowSys* sys = sys_info_[flow_alloc.flow()].get();

    if (!sys->matched.hipri.empty() && !sys->hipri.did_create_class) {
      LOG(ERROR) << "failed to create rate limiter for flow (HIPRI): alloc = "
                 << flow_alloc.ShortDebugString();
      LOG(WARNING) << "will not change iptables config for flow";
      continue;
    } else {
      StageIptablesForFlow(sys->matched.hipri, kDscpHipri, sys->hipri.class_id);
    }

    if (!sys->matched.lopri.empty() && !sys->lopri.did_create_class) {
      LOG(ERROR) << "failed to create rate limiter for flow (LOPRI): alloc = "
                 << flow_alloc.ShortDebugString();
      LOG(WARNING) << "will not change iptables config for flow";
      continue;
    } else {
      StageIptablesForFlow(sys->matched.lopri, kDscpLopri, sys->lopri.class_id);
    }
  }

  st = ipt_controller_.CommitChanges();
  if (!st.ok()) {
    LOG(ERROR) << "failed to commit iptables config: " << st;
    LOG(WARNING) << "will not decrease rate limits";
    return;
  }

  // ==== Stage 3: Decrease any rate limits ====

  tc_batch_input_.Clear();
  create_count = 0;
  update_count = 0;
  for (const proto::FlowAlloc& flow_alloc : bundle.flow_allocs()) {
    FlowSys* sys = sys_info_[flow_alloc.flow()].get();

    if (sys->hipri.update_after_ipt_change) {
      StageTrafficControlForFlow({
          .rate_limit_bps = sys->hipri.cur_rate_limit_bps,
          .sys = &sys->hipri,
          .create_count = &create_count,
          .update_count = &update_count,
      });
    }
    if (sys->lopri.update_after_ipt_change) {
      StageTrafficControlForFlow({
          .rate_limit_bps = sys->lopri.cur_rate_limit_bps,
          .sys = &sys->lopri,
          .create_count = &create_count,
          .update_count = &update_count,
      });
    }
  }
  LOG(INFO) << absl::StrFormat("decreasing %d rate limits", update_count);
  st = tc_caller_.Batch(tc_batch_input_, /*force=*/true);
  if (!st.ok()) {
    LOG(ERROR) << "failed to decrease rate limits for some flows: " << st;
  }
}

bool LinuxHostEnforcerImpl::IsLopri(const proto::FlowMarker& flow) {
  return ipt_controller_.DscpFor(flow.src_port(), flow.dst_port(), flow.dst_addr(),
                                 kDscpHipri) == kDscpLopri;
}

}  // namespace

std::unique_ptr<LinuxHostEnforcer> LinuxHostEnforcer::Create(
    absl::string_view device, const MatchHostFlowsFunc& match_host_flows_fn,
    absl::string_view debug_log_outdir) {
  return absl::make_unique<LinuxHostEnforcerImpl>(device, match_host_flows_fn,
                                                  debug_log_outdir);
}

}  // namespace heyp
