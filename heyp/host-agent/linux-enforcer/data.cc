#include "heyp/host-agent/linux-enforcer/data.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "heyp/io/subprocess.h"
#include "heyp/log/logging.h"
#include "third_party/simdjson/simdjson.h"

namespace heyp {

absl::StatusOr<std::string> FindDeviceResponsibleFor(
    const std::vector<std::string>& ip_addrs, const std::string& ip_bin_name) {
  SubprocessResult out = RunSubprocess(ip_bin_name, {"-json", "addr"});
  if (!out.ok()) {
    return out.ErrorWhenRunning("ip");
  }
  std::string buf(out.out);

  if (absl::StripAsciiWhitespace(buf).empty()) {
    return absl::UnknownError("no ip output");
  }
  simdjson::dom::parser parser;
  auto result = parser.parse(buf);
  if (result.error() != simdjson::SUCCESS) {
    return absl::InternalError(absl::StrCat("failed to parse ip output to json: ",
                                            simdjson::error_message(result.error())));
  }
  auto dev_configs = result.value().get_array().value();
  for (auto dev_config : dev_configs) {
    absl::flat_hash_set<std::string> want_addrs{ip_addrs.begin(), ip_addrs.end()};
    auto dev_maybe = dev_config["ifname"];
    if (dev_maybe.error() != simdjson::SUCCESS) {
      continue;
    }
    std::string_view dev = dev_maybe.get_string().value();
    for (auto addr_info : dev_config["addr_info"].get_array()) {
      auto maybe_local = addr_info["local"];
      if (maybe_local.error() != simdjson::SUCCESS) {
        continue;
      }
      std::string_view local = addr_info["local"].get_string();
      want_addrs.erase(local);
    }

    if (want_addrs.empty()) {
      return std::string(dev);
    }
  }

  return absl::NotFoundError(
      absl::StrCat("requested addrs not found: ", absl::StrJoin(ip_addrs, " ")));
}

}  // namespace heyp
