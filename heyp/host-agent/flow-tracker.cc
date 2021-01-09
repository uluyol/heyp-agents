#include "heyp/host-agent/flow-tracker.h"

#include <algorithm>

#include "absl/status/status.h"
#include "absl/strings/str_split.h"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "boost/process/pipe.hpp"
#include "boost/process/search_path.hpp"
#include "glog/logging.h"
#include "heyp/host-agent/urls.h"

namespace bp = boost::process;

namespace heyp {

FlowTracker::FlowTracker(std::unique_ptr<DemandPredictor> demand_predictor,
                         Config config)
    : config_(config),
      demand_predictor_(std::move(demand_predictor)),
      next_seqnum_(0) {}

void FlowTracker::ForEachActiveFlow(
    absl::FunctionRef<void(const FlowState &)> func) const {
  absl::MutexLock l(&mu_);
  for (const auto &flow_state_pair : active_flows_) {
    func(flow_state_pair.second);
  }
}

void FlowTracker::ForEachFlow(
    absl::FunctionRef<void(const FlowState &)> func) const {
  absl::MutexLock l(&mu_);
  for (const auto &flow_state_pair : active_flows_) {
    func(flow_state_pair.second);
  }
  for (const FlowState &state : done_flows_) {
    func(state);
  }
}

namespace {

FlowState CreateFlowState(const proto::FlowMarker &f, uint64_t seqnum) {
  proto::FlowMarker flow = f;
  flow.set_seqnum(seqnum);
  return FlowState(flow);
}

}  // namespace

void FlowTracker::UpdateFlows(
    absl::Time timestamp,
    absl::Span<const std::pair<proto::FlowMarker, int64_t>>
        flow_usage_bytes_batch) {
  absl::MutexLock lock(&mu_);
  for (size_t i = 0; i < flow_usage_bytes_batch.size();) {
    const proto::FlowMarker &f = flow_usage_bytes_batch[i].first;
    const int64_t cum_usage_bytes = flow_usage_bytes_batch[i].second;
    if (!active_flows_.contains(f)) {
      active_flows_.emplace(f, CreateFlowState(f, ++next_seqnum_));
    }
    FlowState &state = active_flows_.at(f);
    if (state.cum_usage_bytes() > cum_usage_bytes) {
      // Got a race, new usage lower than old usage, so this must be a new flow
      done_flows_.push_back(state);
      active_flows_.erase(f);
      // Rerun on this flow so we add a new state
    } else {
      active_flows_.at(f).UpdateUsage(timestamp, cum_usage_bytes,
                                      config_.usage_history_window,
                                      demand_predictor_.get());
      ++i;
    }
  }
}

void FlowTracker::FinalizeFlows(
    absl::Time timestamp,
    absl::Span<const std::pair<proto::FlowMarker, int64_t>>
        flow_usage_bps_batch) {
  absl::MutexLock lock(&mu_);
  for (const auto &pair : flow_usage_bps_batch) {
    const proto::FlowMarker &f = pair.first;
    if (!active_flows_.contains(f)) {
      active_flows_.emplace(f, CreateFlowState(f, ++next_seqnum_));
    }
    active_flows_.at(f).UpdateUsage(timestamp, pair.second,
                                    config_.usage_history_window,
                                    demand_predictor_.get());
    done_flows_.push_back(active_flows_.at(f));
    active_flows_.erase(f);
  }
}

struct SSFlowStateReporter::Impl {
  const Config config;
  FlowTracker *flow_tracker;

  bp::child monitor_done_proc;
  bp::ipstream monitor_done_out;

  std::thread monitor_done_thread;
  bool is_dying;
};

SSFlowStateReporter::~SSFlowStateReporter() {
  impl_->is_dying = true;
  impl_->monitor_done_proc.terminate();

  if (impl_->monitor_done_thread.joinable()) {
    impl_->monitor_done_thread.join();
  }
}

namespace {

absl::Status ParseLine(uint64_t host_id, absl::string_view line,
                       proto::FlowMarker &parsed, int64_t &cum_usage_bytes) {
  parsed.Clear();

  std::vector<absl::string_view> fields =
      absl::StrSplit(line, absl::ByAnyChar(" \t"), absl::SkipWhitespace());

  absl::string_view src_addr;
  absl::string_view dst_addr;
  int32_t src_port;
  int32_t dst_port;
  absl::Status status = ParseHostPort(fields[3], &src_addr, &src_port);
  status.Update(ParseHostPort(fields[4], &dst_addr, &dst_port));
  if (!status.ok()) {
    return status;
  }

  parsed.set_host_id(host_id);
  parsed.set_src_addr(std::string(src_addr));
  parsed.set_dst_addr(std::string(dst_addr));
  parsed.set_protocol(proto::TCP);
  parsed.set_src_port(src_port);
  parsed.set_dst_port(dst_port);

  bool found_cum_usage_bytes = false;
  bool found_usage_bps = false;
  cum_usage_bytes = 0;
  bool next_is_usage_bps = false;
  int64_t usage_bps = 0;
  for (absl::string_view field : fields) {
    if (absl::StartsWith(field, "bytes_sent:")) {
      field = absl::StripPrefix(field, "bytes_sent:");
      if (!absl::SimpleAtoi(field, &cum_usage_bytes)) {
        return absl::InvalidArgumentError("failed to parse 'bytes_sent' field");
      }
      found_cum_usage_bytes = true;
    }
    if (next_is_usage_bps) {
      if (!absl::EndsWith(field, "bps")) {
        return absl::InvalidArgumentError("failed to parse send bps field");
      }
      field = absl::StripSuffix(field, "bps");
      if (!absl::SimpleAtoi(field, &usage_bps)) {
        return absl::InvalidArgumentError("failed to parse send bps field");
      }
      next_is_usage_bps = false;
      found_usage_bps = true;
    }
    if (field == "send") {
      next_is_usage_bps = true;
    }
  }

  if (!found_cum_usage_bytes) {
    return absl::InvalidArgumentError("no 'bytes_sent' field");
  }
  if (!found_usage_bps) {
    return absl::InvalidArgumentError("no send bps field");
  }

  return absl::OkStatus();
}

}  // namespace

bool SSFlowStateReporter::IgnoreFlow(const proto::FlowMarker &f) {
  bool keep = std::binary_search(impl_->config.my_addrs.begin(),
                                 impl_->config.my_addrs.end(), f.src_addr());
  return !keep;
}

void SSFlowStateReporter::MonitorDone() {
  std::string line;

  while (std::getline(impl_->monitor_done_out, line) && !line.empty()) {
    absl::Time now = absl::Now();
    proto::FlowMarker f;
    int64_t usage_bps = 0;
    auto status = ParseLine(impl_->config.host_id, line, f, usage_bps);
    if (!status.ok()) {
      LOG(ERROR) << "failed to parse done line: " << status;
      continue;
    }
    if (IgnoreFlow(f)) {
      continue;
    }

    impl_->flow_tracker->FinalizeFlows(now, {{f, usage_bps}});
  }

  CHECK(impl_->is_dying);
}

absl::Status SSFlowStateReporter::ReportState() {
  try {
    bp::ipstream out;
    bp::child c(bp::search_path(impl_->config.ss_binary_name), "-i", "-t", "-n",
                "-H", "-O", bp::std_out > out);

    absl::Time now = absl::Now();
    std::string line;
    std::vector<std::pair<proto::FlowMarker, int64_t>> flow_cum_usage_bytes;
    while (std::getline(out, line) && !line.empty()) {
      proto::FlowMarker f;
      int64_t cum_usage_bytes = 0;
      auto status = ParseLine(impl_->config.host_id, line, f, cum_usage_bytes);
      if (!status.ok()) {
        return status;
      }
      if (IgnoreFlow(f)) {
        continue;
      }
      flow_cum_usage_bytes.push_back({f, cum_usage_bytes});
    }
    impl_->flow_tracker->UpdateFlows(now, flow_cum_usage_bytes);
    c.wait();
  } catch (const std::system_error &e) {
    return absl::InternalError(
        absl::StrCat("failed to start ss subprocess: ", e.what()));
  }
  return absl::OkStatus();
}

SSFlowStateReporter::SSFlowStateReporter(Config config,
                                         FlowTracker *flow_tracker)
    : impl_(absl::WrapUnique(new Impl{
          .config = config,
          .flow_tracker = flow_tracker,
          .is_dying = false,
      })) {}

absl::StatusOr<std::unique_ptr<SSFlowStateReporter>>
SSFlowStateReporter::Create(Config config, FlowTracker *flow_tracker) {
  // Sort addresses
  std::sort(config.my_addrs.begin(), config.my_addrs.end());

  auto tracker =
      absl::WrapUnique(new SSFlowStateReporter(config, flow_tracker));

  try {
    bp::child c(bp::search_path(config.ss_binary_name), "-E", "-i", "-t", "-n",
                "-H", "-O", bp::std_out > tracker->impl_->monitor_done_out);
    tracker->impl_->monitor_done_proc = std::move(c);
  } catch (const std::system_error &e) {
    return absl::UnknownError(
        absl::StrCat("failed to start ss subprocess: ", e.what()));
  }

  tracker->impl_->monitor_done_thread =
      std::thread(&SSFlowStateReporter::MonitorDone, tracker.get());

  return tracker;
}

}  // namespace heyp
