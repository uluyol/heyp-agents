#ifndef HEYP_CLUSTER_AGENT_CONTROLLER_H_
#define HEYP_CLUSTER_AGENT_CONTROLLER_H_

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "heyp/proto/heyp.pb.h"

namespace {

class ClusterController {
 public:
  void UpdateInfo(const proto::HostInfo& info);
  absl::flat_hash_map<int64_t, proto::HostAlloc> ComputeAllocs();

 private:
  absl::Mutex mu_;
};

}  // namespace

#endif  // HEYP_CLUSTER_AGENT_CONTROLLER_H_
