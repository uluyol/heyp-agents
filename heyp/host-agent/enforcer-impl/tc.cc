#include "heyp/host-agent/enforcer-impl/tc.h"

#include <limits>

#include "glog/logging.h"
#include "heyp/host-agent/enforcer-impl/iptables.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

namespace {

uint16_t AssertValidPort(int32_t port32) {
  const int32_t kMaxPortNum = std::numeric_limits<uint16_t>::max();

  CHECK(port32 >= 0);
  CHECK(port32 <= kMaxPortNum);

  return static_cast<uint16_t>(port32);
}

}  // namespace

MatchedHostFlows ExpandDestIntoHostsSinglePri(
    const StaticDCMapper *dc_mapper, const FlowStateProvider &flow_state_provider,
    const proto::FlowAlloc &flow_alloc) {
  MatchedHostFlows matched;
  MatchedHostFlows::Vec *expanded = &matched.hipri;
  if (flow_alloc.lopri_rate_limit_bps() > 0) {
    CHECK_EQ(flow_alloc.hipri_rate_limit_bps(), 0)
        << "ExpandDestIntoHostsSinglePri cannot accept both positive hipri and lopri "
           "rate limits";
    expanded = &matched.lopri;
  }
  auto &flow = flow_alloc.flow();
  if (flow.dst_addr().empty()) {
    auto hosts_ptr = dc_mapper->HostsForDC(flow.dst_dc());
    if (hosts_ptr == nullptr) {
      LOG(ERROR) << "no hosts match DC \"" << flow.dst_dc() << "\"";
    } else {
      expanded->reserve(hosts_ptr->size());
      for (const std::string &host : *hosts_ptr) {
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

TcHostEnforcer::TcHostEnforcer(absl::string_view device,
                               const MatchHostFlowsFunc &match_host_flows_fn)
    : device_(device),
      match_host_flows_fn_(match_host_flows_fn),
      ipt_controller_(device),
      next_class_id_(2) {}

absl::Status TcHostEnforcer::ResetDeviceConfig() {
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

absl::Status TcHostEnforcer::ResetIptables() { return ipt_controller_.Clear(); }

absl::Status TcHostEnforcer::ResetTrafficControl() {
  absl::Status st = tc_caller_.Call({"-j", "qdisc", "delete", "dev", device_, "root"});
  st.Update(tc_caller_.Call({"-j", "qdisc", "add", "dev", device_, "root", "handle",
                             "1:", "htb", "default", "0"}));
  return st;
}

static constexpr char kDscpHipri[] = "AF41";
static constexpr char kDscpLopri[] = "AF31";

void TcHostEnforcer::StageIptablesForFlow(const MatchedHostFlows::Vec &matched_flows,
                                          const std::string &dscp,
                                          const std::string &class_id) {
  if (matched_flows.empty()) {
    return;
  }

  CHECK(!class_id.empty()) << "class_id must be set; call UpdateTrafficControlForFlow "
                              "before StageIptablesForFlow";

  for (auto f : matched_flows) {
    ipt_controller_.Stage({
        .dst_port = AssertValidPort(f.dst_port()),
        .src_port = AssertValidPort(f.src_port()),
        .dst_addr = f.dst_addr(),
        .class_id = class_id,
        .dscp = dscp,
    });
  }
}

absl::Status TcHostEnforcer::UpdateTrafficControlForFlow(int64_t rate_limit_bps,
                                                         FlowSys::Priority &sys) {
  double rate_limit_mbps = rate_limit_bps;
  rate_limit_mbps /= 1024.0 * 1024.0;

  if (sys.class_id.empty()) {
    sys.class_id = absl::StrCat("1:", next_class_id_++);
  }

  if (!sys.did_create_class) {
    // Add class
    absl::Status st = tc_caller_.Call({"-j", "class", "add", "dev", device_, "parent",
                                       "1:", "classid", sys.class_id, "htb", "rate",
                                       absl::StrCat(rate_limit_mbps, "mbit")});
    if (st.ok()) {
      sys.did_create_class = true;
    } else {
      return absl::InternalError(
          absl::StrCat("failed to create tc class: ", st.message()));
    }
  } else {
    // Change class rate limit
    absl::Status st = tc_caller_.Call({"-j", "class", "change", "dev", device_, "parent",
                                       "1:", "classid", sys.class_id, "htb", "rate",
                                       absl::StrCat(rate_limit_mbps, "mbit")});
    if (!st.ok()) {
      return absl::InternalError(
          absl::StrCat("failed to change rate limit for tc class: ", st.message()));
    }
  }

  if (!sys.did_create_filter) {
    absl::Status st = tc_caller_.Call({"-j", "class", "add", "dev", device_, "parent",
                                       "1:", "classid", sys.class_id, "htb", "rate",
                                       absl::StrCat(rate_limit_mbps, "mbit")});
    if (st.ok()) {
      sys.did_create_class = true;
    } else {
      return absl::InternalError(
          absl::StrCat("failed to create tc class: ", st.message()));
    }
  }

  return absl::OkStatus();
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
void TcHostEnforcer::EnforceAllocs(const FlowStateProvider &flow_state_provider,
                                   const proto::AllocBundle &bundle) {
  for (const proto::FlowAlloc &flow_alloc : bundle.flow_allocs()) {
    MatchedHostFlows matched = match_host_flows_fn_(flow_state_provider, flow_alloc);
    FlowSys &sys = sys_info_[flow_alloc.flow()];
    absl::Status st = absl::OkStatus();

    // Early update for traffic control (HIPRI)
    bool must_create = sys.hipri.class_id.empty() && !matched.hipri.empty();
    if (must_create || flow_alloc.hipri_rate_limit_bps() > sys.hipri.cur_rate_limit_bps) {
      st = UpdateTrafficControlForFlow(flow_alloc.hipri_rate_limit_bps(), sys.hipri);
      sys.hipri.update_after_ipt_change = false;
    } else if (flow_alloc.hipri_rate_limit_bps() < sys.hipri.cur_rate_limit_bps) {
      sys.hipri.update_after_ipt_change = true;
    }
    sys.hipri.cur_rate_limit_bps = flow_alloc.hipri_rate_limit_bps();

    // Early update for traffic control (LOPRI)
    must_create = sys.lopri.class_id.empty() && !matched.lopri.empty();
    if (must_create || flow_alloc.lopri_rate_limit_bps() > sys.lopri.cur_rate_limit_bps) {
      st.Update(
          UpdateTrafficControlForFlow(flow_alloc.lopri_rate_limit_bps(), sys.lopri));
      sys.lopri.update_after_ipt_change = false;
    } else if (flow_alloc.lopri_rate_limit_bps() < sys.lopri.cur_rate_limit_bps) {
      sys.lopri.update_after_ipt_change = true;
    }
    sys.lopri.cur_rate_limit_bps = flow_alloc.lopri_rate_limit_bps();

    if (!st.ok()) {
      LOG(ERROR) << "failed to increase rate limits for flow: alloc = "
                 << flow_alloc.ShortDebugString() << ": " << st;
      LOG(WARNING) << "will not change iptables config for flow";
      continue;
    }

    StageIptablesForFlow(matched.hipri, kDscpHipri, sys.hipri.class_id);
    StageIptablesForFlow(matched.lopri, kDscpLopri, sys.lopri.class_id);
  }

  absl::Status st = ipt_controller_.CommitChanges();
  if (!st.ok()) {
    LOG(ERROR) << "failed to commit iptables config: " << st;
    LOG(WARNING) << "will not decrease rate limits";
    return;
  }

  for (const proto::FlowAlloc &flow_alloc : bundle.flow_allocs()) {
    FlowSys &sys = sys_info_[flow_alloc.flow()];

    if (sys.hipri.update_after_ipt_change) {
      st = UpdateTrafficControlForFlow(sys.hipri.cur_rate_limit_bps, sys.hipri);
    }
    if (sys.lopri.update_after_ipt_change) {
      st = UpdateTrafficControlForFlow(sys.lopri.cur_rate_limit_bps, sys.lopri);
    }

    if (!st.ok()) {
      LOG(ERROR) << "failed to reduce rate limits for flow: alloc = "
                 << flow_alloc.ShortDebugString() << ": " << st;
      continue;
    }
  }
}

}  // namespace heyp
