#ifndef HEYP_HOST_AGENT_PARSE_SS_H_
#define HEYP_HOST_AGENT_PARSE_SS_H_

#include <cstdint>
#include <string_view>

#include "absl/status/status.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

absl::Status ParseLineSS(uint64_t host_id_to_use, std::string_view line,
                         proto::FlowMarker& flow, int64_t& cur_usage_bps,
                         int64_t& cum_usage_bytes, proto::FlowInfo::AuxInfo* aux);

}

#endif  // HEYP_HOST_AGENT_PARSE_SS_H_
