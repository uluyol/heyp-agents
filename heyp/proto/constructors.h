#ifndef HEYP_PROTO_CONSTRUCTORS_H_
#define HEYP_PROTO_CONSTRUCTORS_H_

#include <cstdint>

#include "absl/base/macros.h"
#include "absl/time/time.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

struct FlowMarkerStruct {
  std::string src_dc;
  std::string dst_dc;
  uint64_t host_id = 0;
  std::string src_addr;
  std::string dst_addr;
  proto::Protocol protocol = proto::Protocol::TCP;
  int32_t src_port = 0;
  int32_t dst_port = 0;
  uint64_t seqnum = 0;
};

inline proto::FlowMarker ProtoFlowMarker(FlowMarkerStruct st);

inline absl::Time FromProtoTimestamp(const google::protobuf::Timestamp& timestamp);

inline google::protobuf::Timestamp ToProtoTimestamp(absl::Time time);

// Implementation

inline proto::FlowMarker ProtoFlowMarker(FlowMarkerStruct st) {
  proto::FlowMarker p;
  p.set_src_dc(st.src_dc);
  p.set_dst_dc(st.dst_dc);
  p.set_host_id(st.host_id);
  p.set_src_addr(st.src_addr);
  p.set_dst_addr(st.dst_addr);
  p.set_protocol(st.protocol);
  p.set_src_port(st.src_port);
  p.set_dst_port(st.dst_port);
  p.set_seqnum(st.seqnum);
  return p;
}

inline absl::Time FromProtoTimestamp(const google::protobuf::Timestamp& timestamp) {
  return absl::FromUnixSeconds(timestamp.seconds()) +
         absl::Nanoseconds(timestamp.nanos());
}

inline google::protobuf::Timestamp ToProtoTimestamp(absl::Time time) {
  google::protobuf::Timestamp timestamp;
  timestamp.set_seconds(absl::ToUnixSeconds(time));
  int64_t nanos =
      absl::ToInt64Nanoseconds(time - absl::FromUnixSeconds(absl::ToUnixSeconds(time)));
  ABSL_ASSERT(nanos < 1'000'000'000);
  timestamp.set_nanos(nanos);
  return timestamp;
}

}  // namespace heyp

#endif  // HEYP_PROTO_CONSTRUCTORS_H_
