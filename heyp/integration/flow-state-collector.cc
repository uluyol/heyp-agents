#include "heyp/integration/flow-state-collector.h"

#include "heyp/log/spdlog.h"

namespace heyp {
namespace testing {
namespace {

void CollectPeriodicReport(absl::Duration period, absl::Notification* done,
                           SSFlowStateReporter* reporter) {
  spdlog::logger logger = MakeLogger("collect-periodic-report");
  while (true) {
    absl::Status st =
        reporter->ReportState([](const proto::FlowMarker&) -> bool { return false; });
    if (!st.ok()) {
      SPDLOG_LOGGER_ERROR(&logger, "failed to collect flow states: {}", st);
    }

    if (done->WaitForNotificationWithTimeout(period)) {
      return;
    }
  }
}

}  // namespace

absl::StatusOr<std::unique_ptr<FlowStateCollector>> FlowStateCollector::Create(
    const std::vector<HostWorker::Flow>& all_flows, absl::Duration period,
    bool ignore_instantaneous_usage) {
  auto collector = absl::WrapUnique(new FlowStateCollector());
  collector->flow_tracker_ = absl::make_unique<FlowTracker>(
      absl::make_unique<BweDemandPredictor>(absl::Seconds(30), 1.1, 1'000'000),
      FlowTracker::Config{.ignore_instantaneous_usage = ignore_instantaneous_usage});
  auto reporter_or = SSFlowStateReporter::Create(
      {
          .host_id = 1,
          .my_addrs = {"127.0.0.1"},
      },
      collector->flow_tracker_.get());
  if (!reporter_or.ok()) {
    return reporter_or.status();
  }
  collector->reporter_ = std::move(*reporter_or);

  for (auto flow : all_flows) {
    collector->src_dst_port_to_name_[{flow.src_port, flow.dst_port}] = flow.name;
  }

  collector->report_thread_ = std::thread(&CollectPeriodicReport, period,
                                          &collector->done_, collector->reporter_.get());

  return collector;
}

FlowStateCollector::FlowStateCollector() {}

void FlowStateCollector::CollectStep(const std::string& label) {
  flow_tracker_->ForEachActiveFlow([this, &label](absl::Time timestamp,
                                                  const proto::FlowInfo& flow_info) {
    const std::string& name =
        src_dst_port_to_name_[{flow_info.flow().src_port(), flow_info.flow().dst_port()}];
    if (name.empty()) {
      return;  // skip unknown flow
    }

    {
      proto::TestCompareMetrics::Metric m;
      m.set_name(absl::StrCat(name, "/", label, "/ss-usage"));
      m.set_value(flow_info.ewma_usage_bps());
      this->measurements_.push_back(std::move(m));
    }
    {
      proto::TestCompareMetrics::Metric m;
      m.set_name(absl::StrCat(name, "/", label, "/ss-demand"));
      m.set_value(flow_info.predicted_demand_bps());
      this->measurements_.push_back(std::move(m));
    }
  });
}

std::vector<proto::TestCompareMetrics::Metric> FlowStateCollector::Finish() {
  done_.Notify();
  report_thread_.join();
  reporter_ = nullptr;

  return measurements_;
}

}  // namespace testing
}  // namespace heyp