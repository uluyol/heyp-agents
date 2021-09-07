#ifndef HEYP_PROTO_RANDOM_H_
#define HEYP_PROTO_RANDOM_H_

#include "google/protobuf/message.h"

namespace heyp {

void FillRandomProto(google::protobuf::Message* mesg);

// Clears a field and returns true if it was previously non-zero.
bool ClearRandomProtoField(google::protobuf::Message* mesg);

}  // namespace heyp

#endif  // HEYP_PROTO_RANDOM_H_
