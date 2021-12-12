#ifndef HEYP_FLOWS_AGG_MARKER_H_
#define HEYP_FLOWS_AGG_MARKER_H_

#include "heyp/proto/heyp.pb.h"

namespace heyp {

proto::FlowMarker ToClusterFlow(const proto::FlowMarker& flow);
proto::FlowMarker ToHostFlow(const proto::FlowMarker& flow);

// Implementation

inline proto::FlowMarker ToClusterFlow(const proto::FlowMarker& flow) {
  proto::FlowMarker h = flow;
  h.clear_job();
  h.clear_host_id();
  h.clear_src_addr();
  h.clear_dst_addr();
  h.clear_protocol();
  h.clear_src_port();
  h.clear_dst_port();
  h.clear_seqnum();
  return h;
}

inline proto::FlowMarker ToHostFlow(const proto::FlowMarker& flow) {
  proto::FlowMarker h = flow;
  h.clear_src_addr();
  h.clear_dst_addr();
  h.clear_protocol();
  h.clear_src_port();
  h.clear_dst_port();
  h.clear_seqnum();
  return h;
}

}  // namespace heyp

#endif  // HEYP_FLOWS_AGG_MARKER_H_
