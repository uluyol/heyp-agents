#include "heyp/proto/alg.h"

#include <cstdint>

#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

namespace heyp {

size_t HashHostFlowNoId::operator()(const proto::FlowMarker& marker) const {
  return absl::Hash<std::tuple<absl::string_view, int32_t, int32_t, int32_t>>()(
      {marker.dst_addr(), marker.protocol(), marker.src_port(),
       marker.dst_port()});
}

}  // namespace heyp
