#include "heyp/host-agent/flow.h"

namespace heyp {

bool operator==(const Flow& lhs, const Flow& rhs) {
  return (lhs.unique_id == rhs.unique_id) && (lhs.protocol == rhs.protocol) &&
         (lhs.src_port == rhs.src_port) && (lhs.dst_addr == rhs.dst_addr) &&
         (lhs.dst_port == rhs.dst_port);
}

}  // namespace heyp
