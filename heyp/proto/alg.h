#ifndef HEYP_PROTO_ALG_H_
#define HEYP_PROTO_ALG_H_

#include "heyp/proto/heyp.pb.h"

namespace heyp {

struct HashHostFlowNoId {
  size_t operator()(const proto::FlowMarker& marker) const;
};

struct EqHostFlowNoId {
  bool operator()(const proto::FlowMarker& lhs,
                  const proto::FlowMarker& rhs) const;
};

}  // namespace heyp

#endif  // HEYP_PROTO_ALG_H_
