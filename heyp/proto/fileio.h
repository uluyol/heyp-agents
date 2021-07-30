#ifndef HEYP_PROTO_FILEIO_H_
#define HEYP_PROTO_FILEIO_H_

#include <cstdio>
#include <string>

#include "absl/status/status.h"
#include "google/protobuf/message.h"

namespace heyp {

bool ReadTextProtoFromFile(const std::string& path, google::protobuf::Message* out);

bool WriteTextProtoToFile(const google::protobuf::Message& message,
                          const std::string& path);

absl::Status WriteJsonLine(const google::protobuf::Message& mesg, int fd);
absl::Status WriteJsonLine(const google::protobuf::Message& mesg, FILE* out);

}  // namespace heyp

#endif  // HEYP_PROTO_FILEIO_H_