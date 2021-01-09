#ifndef HEYP_PROTO_FILEIO_H_
#define HEYP_PROTO_FILEIO_H_

#include <string>

#include "google/protobuf/message.h"

namespace heyp {

bool ReadTextProtoFromFile(const std::string& path,
                           google::protobuf::Message* out);

}  // namespace heyp

#endif  // HEYP_PROTO_FILEIO_H_