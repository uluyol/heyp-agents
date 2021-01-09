#include "heyp/proto/fileio.h"

#include <fcntl.h>

#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

namespace heyp {

bool ReadTextProtoFromFile(const std::string& path,
                           google::protobuf::Message* out) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    return false;
  }
  google::protobuf::io::FileInputStream input(fd);
  input.SetCloseOnDelete(true);
  return google::protobuf::TextFormat::Parse(&input, out);
}

}  // namespace heyp
