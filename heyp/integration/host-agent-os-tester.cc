#include "heyp/integration/host-agent-os-tester.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "glog/logging.h"
#include "heyp/host-agent/enforcer.h"
#include "heyp/host-agent/flow-tracker.h"
#include "heyp/integration/step-worker.h"

namespace heyp {
namespace testing {

HostAgentOSTester::HostAgentOSTester(Config config) : config_(config) {}

namespace {

proto::FlowMarker MarkerOf(const HostWorker::Flow& f) {
  proto::FlowMarker m;
  m.set_host_id(1);
  m.set_src_addr("127.0.0.1");
  m.set_dst_addr("127.0.0.1");
  m.set_protocol(proto::Protocol::TCP);
  m.set_src_port(f.src_port);
  m.set_dst_port(f.dst_port);
  m.set_seqnum(1);
  return m;
}

class InfiniteDemandProvider : public FlowStateProvider {
 public:
  explicit InfiniteDemandProvider(
      const std::vector<HostWorker::Flow>& all_flows) {
    constexpr int64_t kMaxBps = 10'000'000'000;

    for (const HostWorker::Flow& f : all_flows) {
      proto::FlowInfo next;
      *next.mutable_flow() = MarkerOf(f);
      next.set_predicted_demand_bps(kMaxBps);
      next.set_ewma_usage_bps(kMaxBps);
      infos_.push_back(next);
    }
  }

  void ForEachActiveFlow(
      absl::FunctionRef<void(absl::Time, const proto::FlowInfo&)> func)
      const override {
    absl::Time now = absl::Now();
    for (const proto::FlowInfo& info : infos_) {
      func(now, info);
    }
  }

  void ForEachFlow(absl::FunctionRef<void(absl::Time, const proto::FlowInfo&)>
                       func) const override {
    absl::Time now = absl::Now();
    for (const proto::FlowInfo& info : infos_) {
      func(now, info);
    }
  }

 private:
  std::vector<proto::FlowInfo> infos_;
};

class EasyEnforcer {
 public:
  explicit EasyEnforcer(const std::vector<HostWorker::Flow>& all_flows)
      : demand_provider_(all_flows) {}

  void UpdateLimits(
      const std::vector<std::pair<HostWorker::Flow, int64_t>>& limits,
      const std::string& label, proto::TestCompareMetrics* metrics) {
    proto::AllocBundle b;
    for (const auto& flow_limit_pair : limits) {
      proto::FlowAlloc* a = b.add_flow_allocs();
      *a->mutable_flow() = MarkerOf(flow_limit_pair.first);
      a->set_hipri_rate_limit_bps(flow_limit_pair.second);

      auto m = metrics->add_metrics();
      m->set_name(
          absl::StrCat(flow_limit_pair.first.name, "/", label, "/want"));
      m->set_value(flow_limit_pair.second);
    }
    enforcer_.EnforceAllocs(demand_provider_, b);
  }

 private:
  InfiniteDemandProvider demand_provider_;
  HostEnforcer enforcer_;
};

class RateLimitPicker {
 public:
  explicit RateLimitPicker(int64_t max_bps) : max_bps_(max_bps) {
    ABSL_ASSERT(max_bps > 0);
    factor_ = pow(10.0, ceil(log10(max_bps)) - 3);
  }

  void Pick(std::vector<std::pair<HostWorker::Flow, int64_t>>& limits) {
    for (int i = 0; i < limits.size(); ++i) {
      limits[i].second =
          RoundVal(absl::Uniform(gen_, max_bps_ / 100, max_bps_));
    }
  }

 private:
  int64_t RoundVal(int64_t val) {
    if (val == 0) {
      return 0;
    }

    return (val / factor_) * factor_;
  }

  absl::BitGen gen_;
  int64_t max_bps_;
  int64_t factor_;
};

struct HostFlow {
  std::string name;
  int src_port;
  int dst_port;
};

struct FlowFormatter {
  void operator()(std::string* out, const HostWorker::Flow& f) const {
    out->append(absl::StrFormat("Flow %s: port %d -> port %d", f.name,
                                f.src_port, f.dst_port));
  }
};

}  // namespace

// TODO: uh rate limit
absl::StatusOr<proto::TestCompareMetrics> HostAgentOSTester::Run() {
  std::vector<std::unique_ptr<HostWorker>> workers;

  LOG(INFO) << "creating " << config_.num_hosts << " hosts";
  // Populate workers
  for (int i = 0; i < config_.num_hosts; ++i) {
    auto worker_or = HostWorker::Create();
    if (!worker_or.ok()) {
      return worker_or.status();
    }
    workers.push_back(std::move(worker_or.value()));
  }

  LOG(INFO) << "initializing flows";
  // Initialize all flows
  absl::Status status = absl::OkStatus();
  std::vector<HostWorker::Flow> all_flows;
  for (int i = 0; i < config_.num_hosts; ++i) {
    std::vector<HostWorker::Flow> out_flows;
    for (int j = 0; j < config_.num_hosts; ++j) {
      if (i == j) {
        continue;
      }
      out_flows.push_back(HostWorker::Flow{
          .name = absl::StrCat(i, "->", j),
          .dst_port = workers[j]->serve_port(),
      });
    }
    absl::Status init_status = workers[i]->InitFlows(out_flows);
    if (!init_status.ok()) {
      status.Update(init_status);
    } else {
      std::copy(out_flows.begin(), out_flows.end(),
                std::back_inserter(all_flows));
    }
  }

  absl::SleepFor(absl::Milliseconds(100));

  LOG(INFO) << "initalized flows:\n"
            << absl::StrJoin(all_flows, "\n", FlowFormatter());

  proto::TestCompareMetrics metrics;
  if (status.ok()) {
    EasyEnforcer enforcer(all_flows);
    RateLimitPicker limit_picker(config_.max_rate_limit_bps);
    std::vector<std::pair<HostWorker::Flow, int64_t>> all_flows_limits;
    for (const auto& f : all_flows) {
      all_flows_limits.push_back({f, 0});
    }
    limit_picker.Pick(all_flows_limits);
    enforcer.UpdateLimits(all_flows_limits, absl::StrCat("step = ", 0),
                          &metrics);

    for (auto& w : workers) {
      w->Go();
    }

    absl::Time start = absl::Now();

    int step = 0;
    while (absl::Now() - start < config_.run_dur) {
      LOG(INFO) << "step " << step;
      absl::SleepFor(config_.step_dur);
      std::string label(absl::StrCat("step = ", step, "/have"));
      for (auto& w : workers) {
        w->CollectStep(label);
      }
      ++step;
      limit_picker.Pick(all_flows_limits);
      enforcer.UpdateLimits(all_flows_limits, absl::StrCat("step = ", step),
                            &metrics);
    }
  }

  LOG(INFO) << "collecting metrics";
  for (size_t i = 0; i < workers.size(); ++i) {
    LOG(INFO) << "collecting metrics from worker " << i;
    for (auto m : workers[i]->Finish()) {
      *metrics.add_metrics() = m;
    }
  }

  if (!status.ok()) {
    return status;
  }
  return metrics;
}

}  // namespace testing
}  // namespace heyp