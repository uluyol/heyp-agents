#include "heyp/cluster-agent/controller.h"

#include "absl/base/macros.h"
#include "glog/logging.h"
#include "heyp/alg/debug.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/cluster-agent/allocs.h"

namespace heyp {

ClusterController::ClusterController(std::unique_ptr<FlowAggregator> aggregator,
                                     std::unique_ptr<ClusterAllocator> allocator)
    : aggregator_(std::move(aggregator)), allocator_(std::move(allocator)) {}

ClusterController::Listener::Listener() : host_id_(0), controller_(nullptr) {}

ClusterController::Listener::~Listener() {
  if (controller_ != nullptr && host_id_ != 0) {
    absl::MutexLock l(&controller_->broadcasting_mu_);
    ABSL_ASSERT(controller_->on_new_bundle_funcs_.contains(host_id_));
    controller_->on_new_bundle_funcs_.erase(host_id_);
  }
  host_id_ = 0;
  controller_ = nullptr;
}

ClusterController::Listener::Listener(Listener&& other)
    : host_id_(other.host_id_), controller_(other.controller_) {
  other.host_id_ = 0;
  other.controller_ = nullptr;
}

ClusterController::Listener& ClusterController::Listener::operator=(Listener&& other) {
  host_id_ = other.host_id_;
  controller_ = other.controller_;
  other.host_id_ = 0;
  other.controller_ = nullptr;
  return *this;
}

ClusterController::Listener ClusterController::RegisterListener(
    int64_t host_id, const std::function<void(proto::AllocBundle)>& on_new_bundle_func) {
  ClusterController::Listener lis;
  lis.host_id_ = host_id;
  lis.controller_ = this;
  absl::MutexLock l(&broadcasting_mu_);
  on_new_bundle_funcs_[host_id] = on_new_bundle_func;
  return lis;
}

void ClusterController::UpdateInfo(const proto::InfoBundle& info) {
  absl::MutexLock l(&state_mu_);
  aggregator_->Update(info);
}

void ClusterController::ComputeAndBroadcast() {
  const bool should_debug = DebugQosAndRateLimitSelection();
  state_mu_.Lock();
  allocator_->Reset();
  {
    ClusterAllocator* alloc = allocator_.get();
    aggregator_->ForEachAgg(
        [alloc, should_debug](absl::Time time, const proto::AggInfo& info) {
          if (should_debug) {
            LOG(INFO) << "got info: " << info.DebugString();
          }
          alloc->AddInfo(time, info);
        });
  }
  AllocSet allocs = allocator_->GetAllocs();
  state_mu_.Unlock();
  if (should_debug) {
    LOG(INFO) << "got allocs: " << allocs;
  }

  absl::flat_hash_map<int64_t, proto::AllocBundle> alloc_bundles =
      BundleByHost(std::move(allocs));

  broadcasting_mu_.Lock();
  for (auto host_bundle_pair : alloc_bundles) {
    auto iter = on_new_bundle_funcs_.find(host_bundle_pair.first);
    if (iter != on_new_bundle_funcs_.end()) {
      iter->second(std::move(host_bundle_pair.second));
    }
  }
  broadcasting_mu_.Unlock();
}

}  // namespace heyp
