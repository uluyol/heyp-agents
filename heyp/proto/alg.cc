#include "heyp/proto/alg.h"

#include <cstdint>

#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

namespace heyp {

#define SAME_OR_RETURN(field_name)            \
  if (lhs.field_name() != rhs.field_name()) { \
    return false;                             \
  }

bool IsSameFlow(const proto::FlowMarker& lhs, const proto::FlowMarker& rhs,
                CompareFlowOptions options) {
  if (options.cmp_fg) {
    SAME_OR_RETURN(src_dc);
    SAME_OR_RETURN(dst_dc);
  }
  if (options.cmp_src_host) {
    SAME_OR_RETURN(src_addr);
  }
  if (options.cmp_host_flow) {
    SAME_OR_RETURN(dst_addr);
    SAME_OR_RETURN(protocol);
    SAME_OR_RETURN(src_port);
    SAME_OR_RETURN(dst_port);
  }
  if (options.cmp_host_unique_id) {
    SAME_OR_RETURN(host_unique_id);
  }
  return true;
}

size_t HashHostFlowNoId::operator()(const proto::FlowMarker& marker) const {
  return absl::Hash<std::tuple<absl::string_view, int32_t, int32_t, int32_t>>()(
      {marker.dst_addr(), marker.protocol(), marker.src_port(),
       marker.dst_port()});
}

bool EqHostFlowNoId::operator()(const proto::FlowMarker& lhs,
                                const proto::FlowMarker& rhs) const {
  return IsSameFlow(lhs, rhs, {.cmp_fg = false, .cmp_host_unique_id = false});
}

}  // namespace heyp
