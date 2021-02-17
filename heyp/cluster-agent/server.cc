#include "heyp/cluster-agent/server.h"

#include "absl/functional/bind_front.h"
#include "absl/functional/function_ref.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/grpcpp.h"
#include "heyp/cluster-agent/controller.h"

namespace heyp {
namespace {

class WaitableAlloc {
 public:
  void Write(proto::AllocBundle alloc) {
    mu_.Lock();
    alloc_ = std::move(alloc);
    have_alloc_ = true;
    mu_.Unlock();
  }

  void BlockingRead(absl::FunctionRef<void(const proto::AllocBundle& alloc)> func) {
    mu_.LockWhen(absl::Condition(&have_alloc_));
    func(alloc_);
    have_alloc_ = false;
    mu_.Unlock();
  }

 private:
  absl::Mutex mu_;
  bool have_alloc_ ABSL_GUARDED_BY(mu_) = false;
  proto::AllocBundle alloc_ ABSL_GUARDED_BY(mu_);
};

}  // namespace

ClusterAgentService::ClusterAgentService(std::unique_ptr<FlowAggregator> aggregator,
                                         std::unique_ptr<ClusterAllocator> allocator,
                                         absl::Duration control_period)
    : control_period_(control_period),
      controller_(std::move(aggregator), std::move(allocator)) {}

grpc::Status ClusterAgentService::RegisterHost(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<proto::AllocBundle, proto::InfoBundle>* stream) {
  bool registered = false;
  ClusterController::Listener lis;
  WaitableAlloc waitable_alloc;

  while (true) {
    proto::InfoBundle info;
    if (!stream->Read(&info)) {
      break;
    }

    if (!registered) {
      lis = controller_.RegisterListener(
          info.bundler().host_id(),
          absl::bind_front(&WaitableAlloc::Write, &waitable_alloc));
    }

    waitable_alloc.BlockingRead(
        [&stream](const proto::AllocBundle& alloc) { stream->Write(alloc); });
  }
  return grpc::Status::OK;
}

}  // namespace heyp
