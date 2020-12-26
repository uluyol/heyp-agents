#ifndef HEYP_HOST_AGENT_FLOW_H_
#define HEYP_HOST_AGENT_FLOW_H_

#include <cstdint>
#include <string>

namespace heyp {

enum class Protocol {
  kTCP,
  kUDP,
};

// A Flow is uniquely identified by its unique_id.
//
// Other fields may be reused between flows.
struct Flow {
  uint64_t unique_id = 0;
  Protocol protocol = Protocol::kTCP;
  int64_t src_port = 0;
  std::string dst_addr;
  int64_t dst_port = 0;
};

template <typename H>
inline H AbslHashValue(H h, const Flow& f) {
  return H::combine(std::move(h), f.unique_id, f.protocol, f.src_port,
                    f.dst_addr, f.dst_port);
}

bool operator==(const Flow& lhs, const Flow& rhs);

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_FLOW_H_