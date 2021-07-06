#include "heyp/host-agent/urls.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace heyp {

absl::Status ParseHostPort(absl::string_view s, absl::string_view* host, int32_t* port) {
  if (s.empty()) {
    return absl::InvalidArgumentError("empty host:port");
  }
  auto port_separator = s.rfind(":");
  if (port_separator == s.npos) {
    return absl::InvalidArgumentError("port not found");
  }
  absl::string_view port_str = s.substr(port_separator + 1);
  if (!absl::SimpleAtoi(port_str, port)) {
    return absl::InvalidArgumentError(absl::StrCat("invalid port: ", port_str));
  }
  if (port_separator == 0) {
    return absl::InvalidArgumentError("found port but no address");
  }
  if (s[0] == '[') {
    if (port_separator < 3) {
      return absl::InvalidArgumentError("invalid address");
    }
    s = s.substr(1, port_separator - 2);
    if (absl::StartsWith(s, "::ffff:") && !absl::StrContains(s.substr(7), ':')) {
      s = s.substr(7);
    }
    *host = s;
  } else {
    *host = s.substr(0, port_separator);
  }
  return absl::OkStatus();
}

}  // namespace heyp
