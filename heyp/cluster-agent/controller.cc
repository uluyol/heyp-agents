#include "heyp/cluster-agent/controller.h"

#include "absl/base/macros.h"
#include "heyp/alg/debug.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/cluster-agent/allocs.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

ClusterController::ClusterController(std::unique_ptr<FlowAggregator> aggregator,
                                     std::unique_ptr<ClusterAllocator> allocator)
    : aggregator_(std::move(aggregator)),
      allocator_(std::move(allocator)),
      logger_(MakeLogger("cluster-ctlr")),
      next_lis_id_(1) {}

ClusterController::Listener::Listener() : host_id_(0), lis_id_(0), controller_(nullptr) {}

ClusterController::Listener::~Listener() {
  if (controller_ != nullptr && host_id_ != 0) {
    absl::MutexLock l(&controller_->broadcasting_mu_);
    ABSL_ASSERT(controller_->new_bundle_funcs_.contains(host_id_));
    ABSL_ASSERT(controller_->new_bundle_funcs_.at(host_id_).contains(lis_id_));
    controller_->new_bundle_funcs_.at(host_id_).erase(lis_id_);
  }
  host_id_ = 0;
  lis_id_ = 0;
  controller_ = nullptr;
}

std::unique_ptr<ClusterController::Listener> ClusterController::RegisterListener(
    int64_t host_id,
    const std::function<void(const proto::AllocBundle&)>& on_new_bundle_func) {
  auto lis = absl::WrapUnique(new Listener());
  lis->host_id_ = host_id;
  lis->controller_ = this;
  absl::MutexLock l(&broadcasting_mu_);
  lis->lis_id_ = next_lis_id_;
  new_bundle_funcs_[host_id][next_lis_id_] = [this, on_new_bundle_func](
                                                 const proto::AllocBundle& alloc,
                                                 bool wait_completion_enabled) {
    on_new_bundle_func(alloc);
    if (wait_completion_enabled) {
      absl::MutexLock l(&this->broadcast_wait_mu_);
      ++this->num_broadcast_completed_;
      std::cerr << "inc num_broadcast_completed_\n";
    }
  };
  next_lis_id_++;
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

  absl::flat_hash_map<int64_t, proto::AllocBundle> alloc_bundles =
      BundleByHost(std::move(allocs));

  broadcasting_mu_.Lock();
  int num = 0;
  if (enable_wait_for_broadcast_completion_) {
    num_broadcast_completed_ = 0;
  }
  for (auto& [host, bundle] : alloc_bundles) {
    auto iter = new_bundle_funcs_.find(host);
    if (iter != new_bundle_funcs_.end()) {
      for (auto& [id, func] : iter->second) {
        func(bundle, enable_wait_for_broadcast_completion_);
        ++num;
      }
    }
  }
  want_num_broadcast_completed_ = enable_wait_for_broadcast_completion_ ? num : 0;
  broadcasting_mu_.Unlock();
}

void ClusterController::EnableWaitForBroadcastCompletion() {
  enable_wait_for_broadcast_completion_ = true;
}

void ClusterController::WaitForBroadcastCompletion() {
  H_SPDLOG_CHECK(&logger_, enable_wait_for_broadcast_completion_);

  broadcast_wait_mu_.LockWhen(absl::Condition(
      +[](ClusterController* self) {
        std::cerr << "check num_broadcast_completed_ [" << self->num_broadcast_completed_
                  << "] = " << self->want_num_broadcast_completed_ << "\n";
        return self->num_broadcast_completed_ == self->want_num_broadcast_completed_;
      },
      this));
  broadcast_wait_mu_.Unlock();
}

}  // namespace heyp
