#include "heyp/cluster-agent/allocator.h"

#include "heyp/cluster-agent/per-agg-allocators/impl-bwe.h"
#include "heyp/cluster-agent/per-agg-allocators/impl-fixed-host-pattern.h"
#include "heyp/cluster-agent/per-agg-allocators/impl-heyp-sigcomm-20.h"
#include "heyp/cluster-agent/per-agg-allocators/impl-nop.h"
#include "heyp/cluster-agent/per-agg-allocators/impl-simple-downgrade.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/monitoring.pb.h"

namespace heyp {

constexpr int kNumAllocCores = 8;

ClusterAllocator::ClusterAllocator(std::unique_ptr<PerAggAllocator> alloc,
                                   NdjsonLogger* alloc_recorder)
    : alloc_(std::move(alloc)),
      logger_(MakeLogger("cluster-alloc")),
      exec_(kNumAllocCores),
      alloc_recorder_(alloc_recorder) {}

void ClusterAllocator::Reset() {
  absl::MutexLock l(&mu_);
  group_ = exec_.NewTaskGroup();
  allocs_.partial_sets.clear();
}

void ClusterAllocator::AddInfo(absl::Time time, const proto::AggInfo& info) {
  group_->AddTaskNoStatus([time, info, this] {
    proto::DebugAllocRecord record;
    std::vector<proto::FlowAlloc> a =
        this->alloc_->AllocAgg(time, info, record.mutable_debug_state());
    absl::MutexLock l(&this->mu_);
    if (this->alloc_recorder_ != nullptr) {
      record.set_timestamp(absl::FormatTime(time, absl::UTCTimeZone()));
      *record.mutable_info() = info;
      *record.mutable_flow_allocs() = {a.begin(), a.end()};
      absl::Status log_status = this->alloc_recorder_->Write(record);
      if (!log_status.ok()) {
        SPDLOG_LOGGER_WARN(&logger_, "failed to log allocation record: {}", log_status);
      }
    }
    allocs_.partial_sets.push_back(std::move(a));
  });
}

AllocSet ClusterAllocator::GetAllocs() {
  group_->WaitAllNoStatus();
  absl::MutexLock l(&mu_);
  return allocs_;
}

absl::StatusOr<std::unique_ptr<ClusterAllocator>> ClusterAllocator::Create(
    const proto::ClusterAllocatorConfig& config,
    const proto::AllocBundle& cluster_wide_allocs, double demand_multiplier,
    NdjsonLogger* alloc_recorder) {
  ClusterFlowMap<proto::FlowAlloc> cluster_admissions =
      ToAdmissionsMap(cluster_wide_allocs);
  switch (config.type()) {
    case proto::CA_NOP:
      return absl::WrapUnique(
          new ClusterAllocator(std::make_unique<NopAllocator>(), alloc_recorder));
    case proto::CA_BWE:
      return absl::WrapUnique(new ClusterAllocator(
          std::make_unique<BweAggAllocator>(config, std::move(cluster_admissions)),
          alloc_recorder));
    case proto::CA_HEYP_SIGCOMM20:
      return absl::WrapUnique(new ClusterAllocator(
          std::make_unique<HeypSigcomm20Allocator>(config, std::move(cluster_admissions),
                                                   demand_multiplier),
          alloc_recorder));
    case proto::CA_SIMPLE_DOWNGRADE:
      return absl::WrapUnique(new ClusterAllocator(
          std::make_unique<SimpleDowngradeAllocator>(
              config, std::move(cluster_admissions), demand_multiplier),
          alloc_recorder));
    case proto::CA_FIXED_HOST_PATTERN:
      return absl::WrapUnique(new ClusterAllocator(
          std::make_unique<FixedHostPatternAllocator>(config), alloc_recorder));
  }
  std::cerr << "unreachable: got cluster allocator type: " << config.type() << "\n";
  DumpStackTraceAndExit(5);
  return nullptr;
}

}  // namespace heyp
