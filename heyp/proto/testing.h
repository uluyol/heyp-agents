#ifndef HEYP_PROTO_TESTING_H_
#define HEYP_PROTO_TESTING_H_

#include "gmock/gmock.h"
#include "heyp/proto/alg.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

MATCHER_P(AllocBundleEq, other, "") {
  if (arg.flow_allocs_size() != other.flow_allocs_size()) {
    return false;
  }
  for (int i = 0; i < arg.flow_allocs_size(); i++) {
    const proto::FlowAlloc& a = arg.flow_allocs(i);
    const proto::FlowAlloc& b = other.flow_allocs(i);
    if (!IsSameFlow(a.flow(), b.flow())) {
      return false;
    }
    if (a.hipri_rate_limit_bps() != b.hipri_rate_limit_bps()) {
      return false;
    }
    if (a.lopri_rate_limit_bps() != b.lopri_rate_limit_bps()) {
      return false;
    }
  }
  return true;
}

}  // namespace heyp

#endif  // HEYP_PROTO_TESTING_H_
