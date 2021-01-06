#ifndef HEYP_PROTO_ALG_H_
#define HEYP_PROTO_ALG_H_

#include "heyp/proto/heyp.pb.h"

namespace heyp {

// CompareFlowOptions restricts the set of fields to compare when comparing
// FlowMarkers.
struct CompareFlowOptions {
  // Include FlowGroup fields in comparison (src_dc, dst_dc)
  bool cmp_fg = true;

  // Include source host address in comparison (src_addr)
  bool cmp_src_host = true;

  // Include host-level flow information in comparison
  // (dst_addr, protocol, src_port, dst_port).
  bool cmp_host_flow = true;

  // Include source host's unique identifier for the flow in the comparison
  // (host_unique_id).
  bool cmp_host_unique_id = true;
};

bool IsSameFlow(const proto::FlowMarker& lhs, const proto::FlowMarker& rhs,
                CompareFlowOptions options = {});

struct HashHostFlowNoId {
  size_t operator()(const proto::FlowMarker& marker) const;
};

struct EqHostFlowNoId {
  bool operator()(const proto::FlowMarker& lhs,
                  const proto::FlowMarker& rhs) const;
};

}  // namespace heyp

#endif  // HEYP_PROTO_ALG_H_
