#include "heyp/host-agent/flow-tracker.h"

#include <algorithm>
#include <deque>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/str_split.h"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "boost/process/pipe.hpp"
#include "boost/process/search_path.hpp"
#include "heyp/host-agent/parse-ss.h"
#include "heyp/host-agent/urls.h"
#include "heyp/log/spdlog.h"
#include "heyp/threads/mutex-helpers.h"

namespace bp = boost::process;

namespace heyp {

FlowTracker::FlowTracker(std::unique_ptr<DemandPredictor> demand_predictor, Config config)
    : config_(config),
      demand_predictor_(std::move(demand_predictor)),
      logger_(MakeLogger("flow-tracker")),
      next_seqnum_(0) {}

void FlowTracker::ForEachActiveFlow(
    absl::FunctionRef<void(absl::Time, const proto::FlowInfo&)> func) const {
  MutexLockWarnLong l(&mu_, absl::Seconds(1), &logger_, "mu_");
  for (const auto& fs : active_flows_) {
    func(fs.second.updated_time(), fs.second.cur());
  }
}

void FlowTracker::ForEachFlow(
    absl::FunctionRef<void(absl::Time, const proto::FlowInfo&)> func) const {
  MutexLockWarnLong l(&mu_, absl::Seconds(1), &logger_, "mu_");
  for (const auto& fs : active_flows_) {
    func(fs.second.updated_time(), fs.second.cur());
  }
  for (const LeafState& state : done_flows_) {
    func(state.updated_time(), state.cur());
  }
}

namespace {

LeafState CreateLeafState(const proto::FlowMarker& f, uint64_t seqnum) {
  proto::FlowMarker flow = f;
  flow.set_seqnum(seqnum);
  return LeafState(flow);
}

}  // namespace

void FlowTracker::UpdateFlows(absl::Time timestamp,
                              absl::Span<const Update> flow_update_batch) {
  MutexLockWarnLong l(&mu_, absl::Seconds(1), &logger_, "mu_");
  for (size_t i = 0; i < flow_update_batch.size();) {
    const Update& u = flow_update_batch[i];
    if (!active_flows_.contains(u.flow)) {
      SPDLOG_LOGGER_INFO(&logger_, "new active flow: {}", u.flow.ShortDebugString());
      active_flows_.emplace(u.flow, CreateLeafState(u.flow, ++next_seqnum_));
    }
    LeafState& state = active_flows_.at(u.flow);
    if (state.cur().cum_usage_bytes() > u.cum_usage_bytes) {
      // Got a race, new usage lower than old usage, so this must be a new flow
      done_flows_.push_back(state);
      active_flows_.erase(u.flow);
      // Rerun on this flow so we add a new state
    } else {
      H_SPDLOG_CHECK_NE(&logger_, u.used_priority, FlowPri::kUnset);
      H_SPDLOG_CHECK(&logger_,
                     u.used_priority == FlowPri::kHi || u.used_priority == FlowPri::kLo);
      bool is_lopri = u.used_priority == FlowPri::kLo;
      LeafState::Update flow_update{
          .time = timestamp,
          .cum_usage_bytes = u.cum_usage_bytes,
          .instantaneous_usage_bps = u.instantaneous_usage_bps,
          .is_lopri = is_lopri,
          .aux = u.aux,
      };
      if (config_.ignore_instantaneous_usage) {
        flow_update.instantaneous_usage_bps = 0;
      }
      active_flows_.at(u.flow).UpdateUsage(flow_update, config_.usage_history_window,
                                           *demand_predictor_);
      ++i;
    }
  }
}

void FlowTracker::FinalizeFlows(absl::Time timestamp,
                                absl::Span<const Update> flow_update_batch) {
  MutexLockWarnLong l(&mu_, absl::Seconds(1), &logger_, "mu_");
  for (const Update& u : flow_update_batch) {
    if (!active_flows_.contains(u.flow)) {
      SPDLOG_LOGGER_DEBUG(&logger_, "missing active flow: {}", u.flow.ShortDebugString());
      active_flows_.emplace(u.flow, CreateLeafState(u.flow, ++next_seqnum_));
    }
    LeafState& state = active_flows_.at(u.flow);
    bool is_lopri = u.used_priority == FlowPri::kLo;
    if (u.used_priority == FlowPri::kUnset && state.cur().currently_lopri()) {
      is_lopri = true;
    }
    LeafState::Update flow_update{
        .time = timestamp,
        .cum_usage_bytes = u.cum_usage_bytes,
        .instantaneous_usage_bps = u.instantaneous_usage_bps,
        .is_lopri = is_lopri,
        .aux = u.aux,
    };
    if (config_.ignore_instantaneous_usage) {
      flow_update.instantaneous_usage_bps = 0;
    }
    state.UpdateUsage(flow_update, config_.usage_history_window, *demand_predictor_);
    SPDLOG_LOGGER_INFO(&logger_, "moving flow from active to done: {}",
                       u.flow.ShortDebugString());
    done_flows_.push_back(state);
    active_flows_.erase(u.flow);
  }
}

struct SSFlowStateReporter::Impl {
  const Config config;
  FlowTracker* flow_tracker;

  absl::Mutex monitor_done_proc_mu;
  bp::child monitor_done_proc ABSL_GUARDED_BY(monitor_done_proc_mu);
  std::unique_ptr<bp::ipstream> monitor_done_out;

  std::thread monitor_done_thread;
  std::atomic<bool> is_dying;
};

SSFlowStateReporter::~SSFlowStateReporter() {
  impl_->is_dying = true;
  {
    absl::MutexLock l(&impl_->monitor_done_proc_mu);
    impl_->monitor_done_proc.terminate();
  }

  if (impl_->monitor_done_thread.joinable()) {
    impl_->monitor_done_thread.join();
  }

  impl_->monitor_done_out->pipe().close();
}

namespace {

boost::filesystem::path SearchPath(std::string ss_binary_name) {
  if (!absl::StrContains(ss_binary_name, '/')) {
    return bp::search_path(ss_binary_name);
  }
  if (!absl::StartsWith(ss_binary_name, "/")) {
    return boost::filesystem::relative(ss_binary_name);
  }
  return boost::filesystem::path(ss_binary_name);
}

absl::Status StartDoneMonitor(const std::string& ss_binary_name,
                              std::unique_ptr<bp::ipstream>* out, bp::child* proc,
                              absl::Mutex* proc_mu) {
  try {
    absl::MutexLock l(proc_mu);
    *out = absl::make_unique<bp::ipstream>();
    *proc = bp::child(SearchPath(ss_binary_name), "-E", "-i", "-t", "-n", "-H", "-O",
                      bp::std_out > **out);
  } catch (const std::system_error& e) {
    return absl::UnknownError(absl::StrCat(
        "failed to start ss subprocess (path = ", ss_binary_name, "): ", e.what()));
  }
  return absl::OkStatus();
}

}  // namespace

bool SSFlowStateReporter::IgnoreFlow(const proto::FlowMarker& f) {
  bool keep = std::binary_search(impl_->config.my_addrs.begin(),
                                 impl_->config.my_addrs.end(), f.src_addr());
  return !keep;
}

void SSFlowStateReporter::MonitorDone() {
  auto logger = MakeLogger("ss-flow-state-reporter:monitor-done");

  SPDLOG_LOGGER_INFO(&logger, "entered loop");
  absl::Cleanup loop_done = [&logger] { SPDLOG_LOGGER_INFO(&logger, "exited loop"); };

  std::string line;
  while (true) {
    while (std::getline(*impl_->monitor_done_out, line) && !line.empty()) {
      absl::Time now = absl::Now();
      proto::FlowMarker f;
      int64_t usage_bps = 0;
      int64_t cum_usage_bytes = 0;
      proto::FlowInfo::AuxInfo aux_space;
      proto::FlowInfo::AuxInfo* aux = nullptr;
      if (impl_->config.collect_aux) {
        aux = &aux_space;
      }
      auto status =
          ParseLineSS(impl_->config.host_id, line, f, usage_bps, cum_usage_bytes, aux);
      if (!status.ok()) {
        SPDLOG_LOGGER_DEBUG(&logger, "failed to parse done line: {} on {}", status, line);
        continue;
      }
      if (IgnoreFlow(f)) {
        SPDLOG_LOGGER_DEBUG(&logger, "ignoring done flow: {}", f.ShortDebugString());
        continue;
      }
      SPDLOG_LOGGER_DEBUG(&logger, "counting done flow: {}", f.ShortDebugString());

      impl_->flow_tracker->FinalizeFlows(
          now, {{f, usage_bps, cum_usage_bytes, FlowPri::kUnset, aux}});
    }
    if (impl_->is_dying) {
      break;
    } else {
      SPDLOG_LOGGER_WARN(&logger, "restarting ss to monitor done flows");
      if (!StartDoneMonitor(impl_->config.ss_binary_name, &impl_->monitor_done_out,
                            &impl_->monitor_done_proc, &impl_->monitor_done_proc_mu)
               .ok()) {
        absl::SleepFor(absl::Milliseconds(500));
      }
    }
  }
}

absl::Status SSFlowStateReporter::ReportState(
    absl::FunctionRef<bool(const proto::FlowMarker&, spdlog::logger*)> is_lopri) {
  try {
    bp::ipstream out;
    bp::child c(SearchPath(impl_->config.ss_binary_name), "-i", "-t", "-n", "-H", "-O",
                bp::std_out > out);

    absl::Time now = absl::Now();
    std::string line;
    std::deque<proto::FlowInfo::AuxInfo> aux_space;
    std::vector<FlowTracker::Update> flow_updates;
    while (std::getline(out, line) && !line.empty()) {
      proto::FlowMarker f;
      int64_t usage_bps = 0;
      int64_t cum_usage_bytes = 0;
      proto::FlowInfo::AuxInfo* aux = nullptr;
      if (impl_->config.collect_aux) {
        aux_space.push_back({});
        aux = &aux_space.back();
      }
      auto status =
          ParseLineSS(impl_->config.host_id, line, f, usage_bps, cum_usage_bytes, aux);
      if (!status.ok()) {
        SPDLOG_LOGGER_DEBUG(&logger_, "failed to parse line: {} on {}", status, line);
        continue;
      }
      if (IgnoreFlow(f)) {
        SPDLOG_LOGGER_DEBUG(&logger_, "ignoring flow: ", f.ShortDebugString());
        continue;
      }
      SPDLOG_LOGGER_DEBUG(&logger_, "counting flow: ", f.ShortDebugString());
      FlowPri pri = FlowPri::kHi;
      if (is_lopri(f, &logger_)) {
        pri = FlowPri::kLo;
      }
      flow_updates.push_back({f, usage_bps, cum_usage_bytes, pri, aux});
    }
    impl_->flow_tracker->UpdateFlows(now, flow_updates);
    c.wait();
  } catch (const std::system_error& e) {
    return absl::InternalError(absl::StrCat(
        "failed to start ss subprocess (path = ", impl_->config.ss_binary_name,
        "): ", e.what()));
  }
  return absl::OkStatus();
}

SSFlowStateReporter::SSFlowStateReporter(Config config, FlowTracker* flow_tracker)
    : impl_(absl::WrapUnique(new Impl{
          .config = config,
          .flow_tracker = flow_tracker,
          .is_dying = false,
      })),
      logger_(MakeLogger("ss-flow-state-reporter")) {}

absl::StatusOr<std::unique_ptr<SSFlowStateReporter>> SSFlowStateReporter::Create(
    Config config, FlowTracker* flow_tracker) {
  // Sort addresses
  std::sort(config.my_addrs.begin(), config.my_addrs.end());

  auto tracker = absl::WrapUnique(new SSFlowStateReporter(config, flow_tracker));

  absl::Status st = StartDoneMonitor(
      config.ss_binary_name, &tracker->impl_->monitor_done_out,
      &tracker->impl_->monitor_done_proc, &tracker->impl_->monitor_done_proc_mu);
  if (!st.ok()) {
    return st;
  }

  tracker->impl_->monitor_done_thread =
      std::thread(&SSFlowStateReporter::MonitorDone, tracker.get());

  return tracker;
}

}  // namespace heyp
