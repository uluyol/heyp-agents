#include "heyp/proto/fileio.h"

#include <fcntl.h>

#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/json_util.h"
#include "heyp/posix/strerror.h"

namespace heyp {

bool ReadTextProtoFromFile(const std::string& path, google::protobuf::Message* out) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    return false;
  }
  google::protobuf::io::FileInputStream input(fd);
  input.SetCloseOnDelete(true);
  return google::protobuf::TextFormat::Parse(&input, out);
}

bool WriteTextProtoToFile(const google::protobuf::Message& message,
                          const std::string& path) {
  int fd = creat(path.c_str(), 0644);
  google::protobuf::io::FileOutputStream output(fd);
  output.SetCloseOnDelete(true);
  return google::protobuf::TextFormat::Print(message, &output);
}

absl::Status WriteJsonLine(const google::protobuf::Message& mesg, FILE* out) {
  google::protobuf::util::JsonPrintOptions opt;
  opt.add_whitespace = false;
  opt.always_print_primitive_fields = true;
  opt.always_print_enums_as_ints = false;

  std::string data;
  auto st = google::protobuf::util::MessageToJsonString(mesg, &data, opt);
  if (!st.ok()) {
    return absl::Status(static_cast<absl::StatusCode>(st.code()),
                        std::string(st.message()));
  }

  if (fwrite(data.data(), 1, data.size(), out) != data.size()) {
    return absl::InternalError(StrError(errno));
  }
  if (fwrite("\n", 1, 1, out) != 1) {
    return absl::InternalError(StrError(errno));
  }
  return absl::OkStatus();
}

}  // namespace heyp
