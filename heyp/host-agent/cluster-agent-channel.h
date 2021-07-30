#ifndef HEYP_HOST_AGENT_CLUSTER_AGENT_CHANNEL_H_
#define HEYP_HOST_AGENT_CLUSTER_AGENT_CHANNEL_H_

#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/grpcpp.h"
#include "heyp/proto/heyp.grpc.pb.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

class ClusterAgentChannel {
 public:
  explicit ClusterAgentChannel(std::unique_ptr<proto::ClusterAgent::Stub> stub);

  grpc::Status WritesDone();

  grpc::Status Write(const proto::InfoBundle& bundle);

  grpc::Status Read(proto::AllocBundle* bundle);

  void TryCancel();

 private:
  struct StreamInfo {
    grpc::ClientContext ctx;
    std::unique_ptr<grpc::ClientReaderWriter<proto::InfoBundle, proto::AllocBundle>>
        stream;
  };

  std::unique_ptr<proto::ClusterAgent::Stub> stub_;

  absl::Mutex mu_;
  std::shared_ptr<StreamInfo> stream_;
  std::vector<std::weak_ptr<StreamInfo>> all_streams_;  // TODO: garbage collect

  bool read_failed_ ABSL_GUARDED_BY(mu_);
  bool write_failed_ ABSL_GUARDED_BY(mu_);
  bool writes_done_ ABSL_GUARDED_BY(mu_);

  grpc::Status CheckFinishAndResetStream() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void CreateStreamIfNeeded() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_CLUSTER_AGENT_CHANNEL_H_
