#include "heyp/cluster-agent/controller.h"

#include "absl/base/macros.h"
#include "heyp/alg/debug.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/cluster-agent/allocs.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

static constexpr absl::Duration kLongBcastLockDur = absl::Milliseconds(50);
static constexpr absl::Duration kLongStateLockDur = absl::Milliseconds(100);

ClusterController::ClusterController(std::unique_ptr<FlowAggregator> aggregator,
                                     std::unique_ptr<ClusterAllocator> allocator)
    : aggregator_(std::move(aggregator)),
      allocator_(std::move(allocator)),
      logger_(MakeLogger("cluster-ctlr")),
      next_lis_id_(1) {}

ClusterController::Listener::Listener() : host_id_(0), lis_id_(0), controller_(nullptr) {}

ClusterController::Listener::~Listener() {
  if (controller_ != nullptr && host_id_ != 0) {
    MutexLockWarnLong l(&controller_->broadcasting_mu_, kLongBcastLockDur,
                        &controller_->logger_, "broadcasting_mu_ in ~Listenener");
    ABSL_ASSERT(controller_->new_bundle_funcs_.contains(host_id_));
    ABSL_ASSERT(controller_->new_bundle_funcs_.at(host_id_).contains(lis_id_));
    controller_->new_bundle_funcs_.at(host_id_).erase(lis_id_);
  }
  host_id_ = 0;
  lis_id_ = 0;
  controller_ = nullptr;
}

std::unique_ptr<ClusterController::Listener> ClusterController::RegisterListener(
    uint64_t host_id,
    const std::function<void(const proto::AllocBundle&)>& on_new_bundle_func) {
  auto lis = absl::WrapUnique(new Listener());
  lis->host_id_ = host_id;
  lis->controller_ = this;
  MutexLockWarnLong l(&broadcasting_mu_, kLongBcastLockDur, &logger_,
                      "broadcasting_mu_ in RegisterListener");
  lis->lis_id_ = next_lis_id_;
  new_bundle_funcs_[host_id][next_lis_id_] = on_new_bundle_func;
  next_lis_id_++;
  return lis;
}

void ClusterController::UpdateInfo(ParID bundler_id, const proto::InfoBundle& info) {
  aggregator_->Update(bundler_id, info);
}

ParID ClusterController::GetBundlerID(const proto::FlowMarker& bundler) {
  return aggregator_->GetBundlerID(bundler);
}

void ClusterController::ComputeAndBroadcast() {
  const bool should_debug = DebugQosAndRateLimitSelection();
  state_mu_.Lock(kLongStateLockDur, &logger_, "state_mu_ in ComputeAndBroadcast");
  allocator_->Reset();
  {
    ClusterAllocator* alloc = allocator_.get();
    aggregator_->ForEachAgg(
        [alloc, should_debug, this](absl::Time time, const proto::AggInfo& info) {
          if (should_debug) {
            SPDLOG_LOGGER_INFO(&logger_, "got info: {}", info.DebugString());
          }
          alloc->AddInfo(time, info);
        });
  }
  AllocSet allocs = allocator_->GetAllocs();
  state_mu_.Unlock();
  if (should_debug) {
    SPDLOG_LOGGER_INFO(&logger_, "got allocs: {}", allocs);
  }

  absl::flat_hash_map<uint64_t, proto::AllocBundle> alloc_bundles =
      BundleByHost(std::move(allocs));

  broadcasting_mu_.Lock(kLongBcastLockDur, &logger_,
                        "broadcasting_mu_ in ComputeAndBroadcast");
  int num = 0;
  for (auto& [host, bundle] : alloc_bundles) {
    auto iter = new_bundle_funcs_.find(host);
    if (iter != new_bundle_funcs_.end()) {
      for (auto& [id, func] : iter->second) {
        func(bundle);
        ++num;
      }
    }
  }
  broadcasting_mu_.Unlock();
}

}  // namespace heyp
