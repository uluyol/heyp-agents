#ifndef HEYP_PROTO_FORMATTER_H_
#define HEYP_PROTO_FORMATTER_H_

#include <string>
#include <type_traits>

#include "google/protobuf/message.h"

namespace heyp {

template <typename Proto>
struct DebugStringFormatter {
  void operator()(std::string* out, const Proto& mesg) {
    static_assert(std::is_base_of<google::protobuf::Message, Proto>::value,
                  "mesg must be a protobuf message");
    *out += mesg.DebugString();
  }
};

}  // namespace heyp

#endif  // HEYP_PROTO_FORMATTER_H_
