#include "heyp/cluster-agent/server.h"

#include <grpcpp/support/server_callback.h>

#include "absl/strings/str_format.h"
#include "grpcpp/grpcpp.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/threads/mutex-helpers.h"

namespace heyp {

class HostReactor
    : public grpc::ServerBidiReactor<proto::InfoBundle, proto::AllocBundle> {
 public:
  HostReactor(ClusterAgentService* service, grpc::CallbackServerContext* context)
      : peer_(context->peer()),
        service_(service),
        staged_alloc_(&b1_),
        wip_write_alloc_(&b2_),
        wip_write_(false),
        has_staged_(false),
        finished_(false) {
    SPDLOG_LOGGER_INFO(&service->logger_, "{}: new connection from {}", __func__, peer_);
    DoReadLoop();
  }

  void DoReadLoop() { StartRead(&info_); }

  void OnReadDone(bool ok) override {
    MutexLockWarnLong l(&mu_, kLongLockDur, &service_->logger_, "HostReactor.mu_");
    if (finished_) {
      return;
    }
    if (!ok) {
      Finish(grpc::Status::OK);
      finished_ = true;
      return;
    }
    SPDLOG_LOGGER_INFO(&service_->logger_, "got info from {} with {} FGs", peer_,
                       info_.flow_infos_size());

    if (lis_ == nullptr) {
      // Unlock mu_ to avoid a lock cycle.
      // Here, we can acquire mu_, then a lock in ClusterController.
      // Later, when ClusterController calls UpdateAlloc, it is holding its own lock then
      // aquiring mu_.
      //
      // This isn't really a problem for us because ClusterController can't call into us
      // until after we call RegisterListener, and we never acquire the
      // ClusterController's lock until teardown (at which point, we hold no locks).
      //
      // But it's fine to release the lock here and silence this TSan warning.
      // If lis_ is null, then we know that only one read was issued (the first) and no
      // writes, so it's impossible for concurrent operations to take place.
      mu_.Unlock();
      lis_ = service_->controller_->RegisterListener(
          info_.bundler().host_id(),
          [this](const proto::AllocBundle& alloc, const SendBundleAux& aux) {
            SPDLOG_LOGGER_INFO(&service_->logger_, "sending allocs for {} FGs to {}",
                               alloc.flow_allocs_size(), peer_);
            UpdateAlloc(alloc, aux);
          });
      bundler_id_ = service_->controller_->GetBundlerID(info_.bundler());
      mu_.Lock(kLongLockDur, &service_->logger_, "HostReactor.mu_");
    }

    service_->controller_->UpdateInfo(bundler_id_, info_);
    DoReadLoop();
  }

  void UpdateAlloc(const proto::AllocBundle& alloc, const SendBundleAux& aux) {
    MutexLockWarnLong l(&mu_, kLongLockDur, &service_->logger_, "HostReactor.mu_");
    *staged_alloc_ = BundleAndAux{alloc, aux};
    if (!wip_write_ && !finished_) {
      SendAlloc();
    } else {
      has_staged_ = true;
    }
  }

  void OnWriteDone(bool ok) override {
    // TO DEBUG: Do something with wip_write_alloc_->aux

    if (!ok) {
      SPDLOG_LOGGER_ERROR(&service_->logger_, "write failed to {}", peer_);
      if (!finished_) {
        Finish(grpc::Status(grpc::StatusCode::UNKNOWN, "failed write"));
      }
      finished_ = true;
      lis_ = nullptr;
      return;
      // since wip_write_ is still true, we will block all writes
    }
    MutexLockWarnLong l(&mu_, kLongLockDur, &service_->logger_, "HostReactor.mu_");
    wip_write_ = false;
    if (has_staged_ && !finished_) {
      has_staged_ = false;
      SendAlloc();
    }
  }

  void OnDone() override { delete this; }

 private:
  static constexpr absl::Duration kLongLockDur = absl::Milliseconds(5);

  void SendAlloc() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    BundleAndAux* t = staged_alloc_;
    staged_alloc_ = wip_write_alloc_;
    wip_write_alloc_ = t;
    wip_write_ = true;
    StartWrite(&wip_write_alloc_->bundle);
  }

  struct BundleAndAux {
    proto::AllocBundle bundle;
    SendBundleAux aux;
  };

  const std::string peer_;
  ClusterAgentService* service_;
  proto::InfoBundle info_;

  TimedMutex mu_;
  BundleAndAux b1_ ABSL_GUARDED_BY(mu_);
  BundleAndAux b2_ ABSL_GUARDED_BY(mu_);

  // Write state
  BundleAndAux* staged_alloc_ ABSL_GUARDED_BY(mu_);
  BundleAndAux* wip_write_alloc_ ABSL_GUARDED_BY(mu_);
  bool wip_write_ ABSL_GUARDED_BY(mu_);
  bool has_staged_ ABSL_GUARDED_BY(mu_);

  bool finished_;  // only read/written in event loop

  std::unique_ptr<ClusterController::Listener> lis_;
  ParID bundler_id_ = -1;
};

ClusterAgentService::ClusterAgentService(
    const std::shared_ptr<ClusterController>& controller, int id)
    : controller_(std::move(controller)),
      logger_(MakeLogger(absl::StrCat("cluster-agent-svc-", id))) {}

grpc::ServerBidiReactor<proto::InfoBundle, proto::AllocBundle>*
ClusterAgentService::RegisterHost(grpc::CallbackServerContext* context) {
  return new HostReactor(this, context);
}

void RunLoop(const std::shared_ptr<ClusterController>& controller,
             absl::Duration control_period, std::atomic<bool>* should_exit,
             spdlog::logger* logger) {
  while (!should_exit->load()) {
    SPDLOG_LOGGER_INFO(logger, "{}: compute new allocations", __func__);
    controller->ComputeAndBroadcast();
    absl::SleepFor(control_period);
  }
}

}  // namespace heyp
