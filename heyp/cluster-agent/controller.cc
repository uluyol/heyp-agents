#include "heyp/cluster-agent/controller.h"

#include "heyp/cluster-agent/allocator.h"
#include "heyp/cluster-agent/allocs.h"

namespace heyp {

ClusterController::ClusterController(
    std::unique_ptr<FlowAggregator> aggregator,
    std::unique_ptr<ClusterAllocator> allocator)
    : aggregator_(std::move(aggregator)), allocator_(std::move(allocator)) {}

ClusterController::Listener::Listener(int64_t host_id, ClusterController* c)
    : host_id_(host_id), controller_(c) {}

ClusterController::Listener::~Listener() {
  if (controller_ != nullptr && host_id_ != 0) {
    absl::MutexLock l(&controller_->broadcasting_mu_);
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

ClusterController::Listener& ClusterController::Listener::operator=(
    Listener&& other) {
  host_id_ = other.host_id_;
  controller_ = other.controller_;
  other.host_id_ = 0;
  other.controller_ = nullptr;
  return *this;
}

ClusterController::Listener ClusterController::RegisterListener(
    int64_t host_id,
    const std::function<void(const proto::AllocBundle&)>& on_new_bundle_func) {
  ClusterController::Listener lis(host_id, this);
  absl::MutexLock l(&broadcasting_mu_);
  on_new_bundle_funcs_[host_id] = on_new_bundle_func;
  return lis;
}

void ClusterController::UpdateInfo(const proto::InfoBundle& info) {
  absl::MutexLock l(&state_mu_);
  aggregator_->Update(info);
}

void ClusterController::ComputeAndBroadcast() {
  state_mu_.Lock();
  allocator_->Reset();
  {
    ClusterAllocator* alloc = allocator_.get();
    aggregator_->ForEachAgg(
        [alloc](absl::Time time, const proto::AggInfo& info) {
          alloc->AddInfo(time, info);
        });
  }
  AllocSet allocs = allocator_->GetAllocs();
  state_mu_.Unlock();

  absl::flat_hash_map<int64_t, proto::AllocBundle> alloc_bundles =
      BundleByHost(std::move(allocs));

  broadcasting_mu_.Lock();
  for (auto host_bundle_pair : alloc_bundles) {
    auto iter = on_new_bundle_funcs_.find(host_bundle_pair.first);
    if (iter != on_new_bundle_funcs_.end()) {
      iter->second(host_bundle_pair.second);
    }
  }
  broadcasting_mu_.Unlock();
}

}  // namespace heyp
