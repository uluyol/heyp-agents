#ifndef HEYP_CLUSTER_AGENT_CONTROLLER_H_
#define HEYP_CLUSTER_AGENT_CONTROLLER_H_

#include <cstdint>
#include <functional>

#include "heyp/proto/heyp.pb.h"
#include "heyp/threads/par-indexed-map.h"

namespace heyp {

class ClusterController {
 public:
  virtual ~ClusterController() = default;

  virtual void UpdateInfo(ParID bundler_id, const proto::InfoBundle& info) = 0;
  virtual void ComputeAndBroadcast() = 0;

  class Listener {
   public:
    virtual ~Listener() = default;
  };

  // on_new_bundle_func should not block.
  virtual std::unique_ptr<Listener> RegisterListener(
      uint64_t host_id,
      const std::function<void(const proto::AllocBundle&)>& on_new_bundle_func) = 0;

  virtual ParID GetBundlerID(const proto::FlowMarker& bundler) = 0;
};

}  // namespace heyp

#endif  // HEYP_CLUSTER_AGENT_CONTROLLER_H_
