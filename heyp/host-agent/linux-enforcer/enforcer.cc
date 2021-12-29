#include "heyp/host-agent/linux-enforcer/enforcer.h"

#include <algorithm>
#include <limits>
#include <memory>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "google/protobuf/util/message_differencer.h"
#include "heyp/host-agent/linux-enforcer/small-string-set.h"
#include "heyp/io/subprocess.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/alg.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/threads/mutex-helpers.h"

namespace heyp {

MatchedHostFlows ExpandDestIntoHostsSinglePri(
    const StaticDCMapper* dc_mapper, const FlowStateProvider& flow_state_provider,
    const proto::FlowAlloc& flow_alloc, spdlog::logger* logger) {
  MatchedHostFlows matched;
  MatchedHostFlows::Vec* expanded = &matched.hipri;
  if (flow_alloc.lopri_rate_limit_bps() > 0) {
    H_SPDLOG_CHECK_EQ_MESG(logger, flow_alloc.hipri_rate_limit_bps(), 0,
                           "ExpandDestIntoHostsSinglePri cannot accept both positive "
                           "hipri and lopri rate limits");
    expanded = &matched.lopri;
  }
  auto& flow = flow_alloc.flow();
  if (flow.dst_addr().empty()) {
    auto hosts_ptr = dc_mapper->HostsForDC(flow.dst_dc());
    if (hosts_ptr == nullptr) {
      SPDLOG_LOGGER_ERROR(logger, "no hosts match DC \"{}\"", flow.dst_dc());
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

  auto eq_netem = [](const std::optional<proto::NetemConfig>& lhs,
                     const std::optional<proto::NetemConfig>& rhs) {
    if (lhs.has_value() && rhs.has_value()) {
      return google::protobuf::util::MessageDifferencer::Equivalent(*lhs, *rhs);
    }
    return lhs.has_value() == rhs.has_value();
  };

  if (!eq_netem(lhs.netem.hipri, rhs.netem.hipri)) {
    return false;
  }
  if (!eq_netem(lhs.netem.lopri, rhs.netem.lopri)) {
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
            << "\n  ]\n  netem = " << c.netem.ToString() << "\n}";
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
    const SimulatedWanDB::QoSNetemConfig* maybe_config =
        simulated_wan.GetNetem(my_dc, dst);
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

uint16_t AssertValidPort(int32_t port32, spdlog::logger* logger) {
  const int32_t kMaxPortNum = std::numeric_limits<uint16_t>::max();

  H_SPDLOG_CHECK(logger, port32 >= 0);
  H_SPDLOG_CHECK(logger, port32 <= kMaxPortNum);

  return static_cast<uint16_t>(port32);
}

constexpr int64_t kMaxBandwidthBps = 100 * (static_cast<int64_t>(1) << 30);  // 100 Gbps

template <typename T>
const T* OptionalToPointer(const std::optional<T>& o) {
  if (o.has_value()) {
    return &o.value();
  }
  return nullptr;
}

absl::string_view NetemDistToString(proto::NetemDelayDist dist) {
  switch (dist) {
    case proto::NETEM_NO_DIST:
      break;
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

}  // namespace

LinuxHostEnforcer::LinuxHostEnforcer(absl::string_view device,
                                     const MatchHostFlowsFunc& match_host_flows_fn,
                                     const proto::HostEnforcerConfig& config,
                                     std::unique_ptr<TcCallerIface> tc_caller,
                                     std::unique_ptr<iptables::RunnerIface> ipt_runner)
    : config_(config),
      device_(device),
      match_host_flows_fn_(match_host_flows_fn),
      logger_(MakeLogger("linux-host-enforcer")),
      tc_caller_(std::move(tc_caller)),
      ipt_controller_(device, SmallStringSet({}), std::move(ipt_runner)),
      debug_logger_(config.debug_log_dir()),
      next_class_id_(2) {
  tc_caller_->SetLogger(&logger_);
}

LinuxHostEnforcer::LinuxHostEnforcer(absl::string_view device,
                                     const MatchHostFlowsFunc& match_host_flows_fn,
                                     const proto::HostEnforcerConfig& config)
    : LinuxHostEnforcer(device, match_host_flows_fn, config, std::make_unique<TcCaller>(),
                        iptables::Runner::Create(iptables::IpFamily::kIpV4)) {}

absl::Status LinuxHostEnforcer::ResetDeviceConfig() {
  MutexLockWarnLong l(&mu_, absl::Seconds(1), &logger_, "mu_");

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

absl::Status LinuxHostEnforcer::InitSimulatedWan(std::vector<FlowNetemConfig> configs,
                                                 bool contains_all_flows) {
  MutexLockWarnLong l(&mu_, absl::Seconds(1), &logger_, "mu_");

  int num_created_classes = 0;
  int num_updated_classes = 0;

  std::vector<std::string> errors;
  tc_batch_input_.Clear();
  for (const FlowNetemConfig& c : configs) {
    if (GetSysInfo(c.flow) != nullptr) {
      errors.push_back(
          absl::StrCat("FlowSys already exists for flow: ", c.flow.ShortDebugString()));
      continue;
    }
    FlowSys* sys = GetOrCreateSysInfo(c.flow);
    StageTrafficControlForFlow({
        .rate_limit_bps = kMaxBandwidthBps,
        .netem_config = OptionalToPointer(c.netem.hipri),
        .sys = &sys->hipri,
        .create_count = &num_created_classes,
        .update_count = &num_updated_classes,
    });
    StageTrafficControlForFlow({
        .rate_limit_bps = kMaxBandwidthBps,
        .netem_config = OptionalToPointer(c.netem.lopri),
        .sys = &sys->lopri,
        .create_count = &num_created_classes,
        .update_count = &num_updated_classes,
    });
  }

  SPDLOG_LOGGER_INFO(&logger_, "creating {} rate limiters and netem qdiscs",
                     num_created_classes);
  if (num_updated_classes > 0) {
    SPDLOG_LOGGER_ERROR(&logger_, "{}: impossible state: updating rate limiters",
                        __func__);
  }
  absl::Status st = tc_caller_->Batch(tc_batch_input_, /*force=*/true);

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

  // ==== Stage 2: set up iptables ====

  for (const FlowNetemConfig& c : configs) {
    FlowSys* sys = GetSysInfo(c.flow);
    sys->hipri.did_create_class = true;
    sys->lopri.did_create_class = true;

    sys->matched.hipri = {c.matched_flows.begin(), c.matched_flows.end()};
    StageIptablesForFlow(sys->matched.hipri, config_.dscp_hipri(), sys->hipri.class_id);
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

  snapshot_mu_.Lock();
  ipt_snapshot_ = ipt_controller_.AppliedSettings();
  snapshot_mu_.Unlock();

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

LinuxHostEnforcer::FlowSys* LinuxHostEnforcer::GetSysInfo(const proto::FlowMarker& flow) {
  auto iter = sys_info_.find(flow);
  if (iter == sys_info_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

LinuxHostEnforcer::FlowSys* LinuxHostEnforcer::GetOrCreateSysInfo(
    const proto::FlowMarker& flow) {
  auto iter = sys_info_.find(flow);
  if (iter != sys_info_.end()) {
    return iter->second.get();
  }
  proto::FlowMarker f = flow;
  f.clear_job();
  sys_info_[f] = std::make_unique<FlowSys>();
  return sys_info_[f].get();
}

absl::Status LinuxHostEnforcer::ResetIptables() { return ipt_controller_.Clear(); }

absl::Status LinuxHostEnforcer::ResetTrafficControl() {
  // First, delete the root qdisc if it exists.
  // Unfortunately, I don't know how to delete the qdisc only if it exists and otherwise
  // skip. So always try to delete and ignore any errors.
  tc_caller_->Call({"qdisc", "delete", "dev", device_, "root"}, false).IgnoreError();
  return tc_caller_->Call(
      {"qdisc", "add", "dev", device_, "root", "handle", "1:", "htb", "default", "0"},
      false);
}

void LinuxHostEnforcer::StageIptablesForFlow(const MatchedHostFlows::Vec& matched_flows,
                                             const std::string& dscp,
                                             const std::string& class_id) {
  if (matched_flows.empty()) {
    return;
  }

  H_SPDLOG_CHECK_MESG(&logger_, !class_id.empty(),
                      "class_id must be set; call UpdateTrafficControlForFlow "
                      "before StageIptablesForFlow");

  for (auto f : matched_flows) {
    ipt_controller_.Stage({
        .src_port = AssertValidPort(f.src_port(), &logger_),
        .dst_port = AssertValidPort(f.dst_port(), &logger_),
        .dst_addr = f.dst_addr(),
        .class_id = class_id,
        .dscp = dscp,
    });
  }
}

void LinuxHostEnforcer::StageTrafficControlForFlow(StageTrafficControlForFlowArgs args) {
  double rate_limit_mbps = std::max(args.rate_limit_bps, config_.min_rate_limit_bps());
  rate_limit_mbps /= 1024.0 * 1024.0;

  if (args.sys->class_id.empty()) {
    args.sys->class_id = absl::StrCat("1:", next_class_id_++);
  }

  std::string burst_arg = "";
  if (config_.pacing_burst_bytes() > 0) {
    burst_arg = absl::StrFormat(" burst %db", config_.pacing_burst_bytes());
  }

  if (!args.sys->did_create_class) {
    tc_batch_input_.Append(
        absl::StrFormat("class add dev %s parent 1: classid %s htb rate %fmbit%s\n",
                        device_, args.sys->class_id, rate_limit_mbps, burst_arg));
    if (args.netem_config != nullptr) {
      std::string netem_handle =
          absl::StrCat(absl::StripPrefix(args.sys->class_id, "1:"), ":0");
      std::string dist_str = "";
      if (args.netem_config->delay_dist() != proto::NETEM_NO_DIST) {
        dist_str = absl::StrFormat(" %dms %f%% distribution %s",
                                   args.netem_config->delay_jitter_ms(),
                                   args.netem_config->delay_correlation_pct(),
                                   NetemDistToString(args.netem_config->delay_dist()));
      }
      tc_batch_input_.Append(absl::StrFormat(
          "qdisc add dev %s parent %s handle %s netem delay %dms%s\n", device_,
          args.sys->class_id, netem_handle, args.netem_config->delay_ms(), dist_str));
    }
    if (args.classes_to_create != nullptr) {
      args.classes_to_create->push_back(args.sys);
    }
    (*args.create_count)++;
  } else {
    tc_batch_input_.Append(
        absl::StrFormat("class change dev %s parent 1: classid %s htb rate %fmbit%s\n",
                        device_, args.sys->class_id, rate_limit_mbps, burst_arg));
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
void LinuxHostEnforcer::EnforceAllocs(const FlowStateProvider& flow_state_provider,
                                      const proto::AllocBundle& bundle) {
  MutexLockWarnLong l(&mu_, absl::Seconds(1), &logger_, "mu_");
  // ==== Stage 1: Initialize qdiscs and increase any rate limits ====

  std::vector<FlowSys::Priority*> classes_to_create;
  tc_batch_input_.Clear();
  int create_count = 0;
  int update_count = 0;
  std::vector<std::string> flows_with_created_classes;  // for logging
  for (const proto::FlowAlloc& flow_alloc : bundle.flow_allocs()) {
    FlowSys* sys = GetOrCreateSysInfo(flow_alloc.flow());
    sys->matched = match_host_flows_fn_(flow_state_provider, flow_alloc, &logger_);

    // Early update for traffic control (HIPRI)
    bool must_create = sys->hipri.class_id.empty() && !sys->matched.hipri.empty();
    if (must_create ||
        flow_alloc.hipri_rate_limit_bps() > sys->hipri.cur_rate_limit_bps) {
      auto limit = flow_alloc.hipri_rate_limit_bps();
      if (!config_.limit_hipri()) {
        limit = kMaxBandwidthBps;
      }
      StageTrafficControlForFlow({
          .rate_limit_bps = limit,
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
      auto limit = flow_alloc.lopri_rate_limit_bps();
      if (!config_.limit_lopri()) {
        limit = kMaxBandwidthBps;
      }
      auto old_create_count = create_count;
      StageTrafficControlForFlow({
          .rate_limit_bps = limit,
          .sys = &sys->lopri,
          .classes_to_create = &classes_to_create,
          .create_count = &create_count,
          .update_count = &update_count,
      });
      if (old_create_count != create_count) {
        flows_with_created_classes.push_back("flow {\n" +
                                             flow_alloc.flow().DebugString() + "}");
      }
      sys->lopri.update_after_ipt_change = false;
    } else if (flow_alloc.lopri_rate_limit_bps() < sys->lopri.cur_rate_limit_bps) {
      sys->lopri.update_after_ipt_change = true;
    }
    sys->lopri.cur_rate_limit_bps = flow_alloc.lopri_rate_limit_bps();
  }

  SPDLOG_LOGGER_INFO(&logger_,
                     "creating {} rate limiters and increasing {} rate limits; flows "
                     "with new limiters:{}{}",
                     create_count, update_count, create_count > 0 ? "\n" : "",
                     absl::StrJoin(flows_with_created_classes, "\n"));
  absl::Status st = tc_caller_->Batch(tc_batch_input_, /*force=*/true);
  if (!st.ok()) {
    SPDLOG_LOGGER_ERROR(&logger_,
                        "failed to init or increase rate limits for some flows: {}", st);
    // Find out which classes have not been created
    st = tc_caller_->Call({"class", "show", "dev", device_}, false);

    if (st.ok()) {
      std::vector<std::string> found_classes;
      for (auto line : absl::StrSplit(tc_caller_->RawOut(), '\n')) {
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
    FlowSys* sys = GetSysInfo(flow_alloc.flow());

    if (!sys->matched.hipri.empty() && !sys->hipri.did_create_class) {
      SPDLOG_LOGGER_ERROR(&logger_,
                          "failed to create rate limiter for flow (HIPRI): alloc = {}",
                          flow_alloc.ShortDebugString());
      SPDLOG_LOGGER_WARN(&logger_, "will not change iptables config for flow");
      continue;
    } else {
      StageIptablesForFlow(sys->matched.hipri, config_.dscp_hipri(), sys->hipri.class_id);
    }

    if (!sys->matched.lopri.empty() && !sys->lopri.did_create_class) {
      SPDLOG_LOGGER_ERROR(&logger_,
                          "failed to create rate limiter for flow (LOPRI): alloc = {}",
                          flow_alloc.ShortDebugString());
      SPDLOG_LOGGER_WARN(&logger_, "will not change iptables config for flow");
      continue;
    } else {
      StageIptablesForFlow(sys->matched.lopri, config_.dscp_lopri(), sys->lopri.class_id);
    }
  }

  st = ipt_controller_.CommitChanges();
  if (!st.ok()) {
    SPDLOG_LOGGER_ERROR(&logger_, "failed to commit iptables config: ", st);
    SPDLOG_LOGGER_WARN(&logger_, "will not decrease rate limits");
  } else {
    snapshot_mu_.Lock();
    ipt_snapshot_ = ipt_controller_.AppliedSettings();
    snapshot_mu_.Unlock();

    // ==== Stage 3: Decrease any rate limits ====

    tc_batch_input_.Clear();
    create_count = 0;
    update_count = 0;
    for (const proto::FlowAlloc& flow_alloc : bundle.flow_allocs()) {
      FlowSys* sys = GetSysInfo(flow_alloc.flow());

      if (sys->hipri.update_after_ipt_change) {
        auto limit = sys->hipri.cur_rate_limit_bps;
        if (!config_.limit_hipri()) {
          limit = kMaxBandwidthBps;
        }
        StageTrafficControlForFlow({
            .rate_limit_bps = limit,
            .sys = &sys->hipri,
            .create_count = &create_count,
            .update_count = &update_count,
        });
      }
      if (sys->lopri.update_after_ipt_change) {
        auto limit = sys->lopri.cur_rate_limit_bps;
        if (!config_.limit_lopri()) {
          limit = kMaxBandwidthBps;
        }
        StageTrafficControlForFlow({
            .rate_limit_bps = limit,
            .sys = &sys->lopri,
            .create_count = &create_count,
            .update_count = &update_count,
        });
      }
    }
    SPDLOG_LOGGER_INFO(&logger_, "decreasing {} rate limits", update_count);
    st = tc_caller_->Batch(tc_batch_input_, /*force=*/true);
    if (!st.ok()) {
      SPDLOG_LOGGER_ERROR(&logger_, "failed to decrease rate limits for some flows: {}",
                          st);
    }
  }
}

void LinuxHostEnforcer::LogState() {
  MutexLockWarnLong l(&mu_, absl::Seconds(1), &logger_, "mu_");

  if (debug_logger_.should_log()) {
    SPDLOG_LOGGER_INFO(&logger_, "debug logging: gather iptables state");
    absl::Cord mangle_table;
    ipt_controller_.GetRunner()
        .SaveInto(iptables::Table::kMangle, mangle_table)
        .IgnoreError();
    SPDLOG_LOGGER_INFO(&logger_, "debug logging: gather tc qdisc state");
    bool have_qdisc_output = false;
    absl::Cord qdisc_output;
    if (tc_caller_->Call({"qdisc"}, false).ok()) {
      qdisc_output.Append(tc_caller_->RawOut());
      have_qdisc_output = true;
    }
    SPDLOG_LOGGER_INFO(&logger_, "debug logging: gather tc class state");
    bool have_class_output = false;
    absl::Cord class_output;
    if (tc_caller_->Call({"class", "show", "dev", device_}, false).ok()) {
      class_output.Append(tc_caller_->RawOut());
      have_class_output = true;
    }

    SPDLOG_LOGGER_INFO(&logger_, "debug logging: write iptables state");
    absl::Time timestamp = absl::Now();
    debug_logger_.Write("iptables:mangle", mangle_table, timestamp);
    if (have_qdisc_output) {
      SPDLOG_LOGGER_INFO(&logger_, "debug logging: write tc qdisc state");
      debug_logger_.Write("tc:qdisc", qdisc_output, timestamp);
    } else {
      SPDLOG_LOGGER_INFO(&logger_, "debug logging: missing tc qdisc state");
    }
    if (have_class_output) {
      SPDLOG_LOGGER_INFO(&logger_, "debug logging: write tc class state");
      debug_logger_.Write("tc:class", class_output, timestamp);
    } else {
      SPDLOG_LOGGER_INFO(&logger_, "debug logging: missing tc class state");
    }
  }
}

IsLopriFunc LinuxHostEnforcer::GetIsLopriFunc() const {
  snapshot_mu_.Lock();
  iptables::SettingBatch settings = ipt_snapshot_;
  snapshot_mu_.Unlock();
  std::string dscp_hipri = config_.dscp_hipri();
  std::string dscp_lopri = config_.dscp_lopri();
  return [settings, dscp_hipri, dscp_lopri](const proto::FlowMarker& flow,
                                            spdlog::logger* logger) {
    return iptables::SettingsFindDscp(settings, flow.src_port(), flow.dst_port(),
                                      flow.dst_addr(), dscp_hipri) == dscp_lopri;
  };
}

}  // namespace heyp
