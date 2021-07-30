#include "heyp/proto/fileio.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>

#include "absl/functional/function_ref.h"
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

static absl::Status WriteJsonLineInternal(
    const google::protobuf::Message& mesg,
    absl::FunctionRef<absl::Status(char* buf, size_t size)> write_fn) {
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

  data.push_back('\n');

  return write_fn(data.data(), data.size());
}

absl::Status WriteJsonLine(const google::protobuf::Message& mesg, int fd) {
  return WriteJsonLineInternal(mesg, [fd](char* buf, size_t bufsiz) {
    size_t total = 0;
    while (total < bufsiz) {
      ssize_t wrote = write(fd, buf, bufsiz);
      if (wrote == -1) {
        if (errno == EINTR) {
          continue;
        }
        return absl::InternalError(StrError(errno));
      }
      total += wrote;
    }
    return absl::OkStatus();
  });
}

absl::Status WriteJsonLine(const google::protobuf::Message& mesg, FILE* out) {
  return WriteJsonLineInternal(mesg, [out](char* buf, size_t bufsiz) {
    if (fwrite(buf, 1, bufsiz, out) != bufsiz) {
      return absl::InternalError(StrError(errno));
    }

    return absl::OkStatus();
  });
}

}  // namespace heyp
