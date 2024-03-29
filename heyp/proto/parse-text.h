#ifndef HEYP_PROTO_PARSE_TEXT_H_
#define HEYP_PROTO_PARSE_TEXT_H_

#include <string>

#include "google/protobuf/text_format.h"
#include "heyp/log/spdlog.h"

namespace heyp {

template <typename ProtoT>
ProtoT ParseTextProto(const std::string& str) {
  ProtoT mesg;
  H_ASSERT(google::protobuf::TextFormat::ParseFromString(str, &mesg));
  return mesg;
}

}  // namespace heyp

#endif  // HEYP_PROTO_PARSE_TEXT_H_
