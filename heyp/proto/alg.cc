#include "heyp/proto/alg.h"

#include <cstdint>

#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

namespace heyp {
namespace {
template <typename T>
bool IsZero(T val) {
  return val == T{};
}
}  // namespace

#define NONZERO_OR_RETURN(field_name) \
  if (IsZero(marker.field_name())) {  \
    return false;                     \
  }

#define NONEMPTY_OR_RETURN(field_name) \
  if (marker.field_name().empty()) {   \
    return false;                      \
  }

bool ExpectedFieldsAreSet(const proto::FlowMarker& marker, CompareFlowOptions options) {
  if (options.cmp_fg) {
    NONEMPTY_OR_RETURN(src_dc);
    NONEMPTY_OR_RETURN(dst_dc);
  }
  if (options.cmp_job) {
    NONEMPTY_OR_RETURN(job);
  }
  if (options.cmp_src_host) {
    NONZERO_OR_RETURN(host_id);
  }
  if (options.cmp_host_flow) {
    NONEMPTY_OR_RETURN(src_addr);
    NONEMPTY_OR_RETURN(dst_addr);
    NONZERO_OR_RETURN(protocol);
    NONZERO_OR_RETURN(src_port);
    NONZERO_OR_RETURN(dst_port);
  }
  if (options.cmp_seqnum) {
    NONZERO_OR_RETURN(seqnum);
  }
  return true;
}

#undef NONZERO_OR_RETURN
#undef NONEMPTY_OR_RETURN

#define ZERO_OR_RETURN(field_name)    \
  if (!IsZero(marker.field_name())) { \
    return false;                     \
  }

#define EMPTY_OR_RETURN(field_name)   \
  if (!marker.field_name().empty()) { \
    return false;                     \
  }

bool UnexpectedFieldsAreUnset(const proto::FlowMarker& marker,
                              CompareFlowOptions options) {
  if (!options.cmp_fg) {
    EMPTY_OR_RETURN(src_dc);
    EMPTY_OR_RETURN(dst_dc);
  }
  if (options.cmp_job) {
    EMPTY_OR_RETURN(job);
  }
  if (!options.cmp_src_host) {
    ZERO_OR_RETURN(host_id);
  }
  if (!options.cmp_host_flow) {
    EMPTY_OR_RETURN(src_addr);
    EMPTY_OR_RETURN(dst_addr);
    ZERO_OR_RETURN(protocol);
    ZERO_OR_RETURN(src_port);
    ZERO_OR_RETURN(dst_port);
  }
  if (!options.cmp_seqnum) {
    ZERO_OR_RETURN(seqnum);
  }
  return true;
}

#undef ZERO_OR_RETURN
#undef EMPTY_OR_RETURN

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
  if (options.cmp_job) {
    SAME_OR_RETURN(job);
  }
  if (options.cmp_src_host) {
    SAME_OR_RETURN(host_id);
  }
  if (options.cmp_host_flow) {
    SAME_OR_RETURN(src_addr);
    SAME_OR_RETURN(dst_addr);
    SAME_OR_RETURN(protocol);
    SAME_OR_RETURN(src_port);
    SAME_OR_RETURN(dst_port);
  }
  if (options.cmp_seqnum) {
    SAME_OR_RETURN(seqnum);
  }
  return true;
}

#undef SAME_OR_RETURN

size_t HashFlow::operator()(const proto::FlowMarker& marker) const {
  return absl::Hash<std::tuple<absl::string_view, absl::string_view, absl::string_view,
                               uint64_t, absl::string_view, absl::string_view, int32_t,
                               int32_t, int32_t, uint64_t>>()(
      {marker.src_dc(), marker.dst_dc(), marker.job(), marker.host_id(),
       marker.src_addr(), marker.dst_addr(), marker.protocol(), marker.src_port(),
       marker.dst_port(), marker.seqnum()});
}

bool EqFlow::operator()(const proto::FlowMarker& lhs,
                        const proto::FlowMarker& rhs) const {
  return IsSameFlow(lhs, rhs, {});
}

size_t HashHostFlowNoId::operator()(const proto::FlowMarker& marker) const {
  return absl::Hash<
      std::tuple<absl::string_view, absl::string_view, int32_t, int32_t, int32_t>>()(
      {marker.src_addr(), marker.dst_addr(), marker.protocol(), marker.src_port(),
       marker.dst_port()});
}

bool EqHostFlowNoId::operator()(const proto::FlowMarker& lhs,
                                const proto::FlowMarker& rhs) const {
  return IsSameFlow(lhs, rhs, {.cmp_fg = false, .cmp_job = false, .cmp_seqnum = false});
}

size_t HashFlowNoJob::operator()(const proto::FlowMarker& marker) const {
  return absl::Hash<
      std::tuple<absl::string_view, absl::string_view, uint64_t, absl::string_view,
                 absl::string_view, int32_t, int32_t, int32_t, uint64_t>>()(
      {marker.src_dc(), marker.dst_dc(), marker.host_id(), marker.src_addr(),
       marker.dst_addr(), marker.protocol(), marker.src_port(), marker.dst_port(),
       marker.seqnum()});
}

bool EqFlowNoJob::operator()(const proto::FlowMarker& lhs,
                             const proto::FlowMarker& rhs) const {
  return IsSameFlow(lhs, rhs, {.cmp_job = false});
}

size_t HashClusterFlow::operator()(const proto::FlowMarker& marker) const {
  return absl::Hash<std::tuple<absl::string_view, absl::string_view>>()(
      {marker.src_dc(), marker.dst_dc()});
}

bool EqClusterFlow::operator()(const proto::FlowMarker& lhs,
                               const proto::FlowMarker& rhs) const {
  return IsSameFlow(lhs, rhs,
                    {
                        .cmp_job = false,
                        .cmp_src_host = false,
                        .cmp_host_flow = false,
                        .cmp_seqnum = false,
                    });
}

}  // namespace heyp
