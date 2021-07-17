#include "heyp/host-agent/linux-enforcer/data.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "boost/process/args.hpp"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "boost/process/pipe.hpp"
#include "boost/process/search_path.hpp"
#include "heyp/log/logging.h"
#include "third_party/simdjson/simdjson.h"

namespace bp = boost::process;

namespace heyp {

absl::StatusOr<std::string> FindDeviceResponsibleFor(
    const std::vector<std::string>& ip_addrs, const std::string& ip_bin_name) {
  try {
    VLOG(2) << "running ip: " << ip_bin_name << "-json addr";

    bp::ipstream out;
    bp::child c(bp::search_path(ip_bin_name), "-json", "addr", bp::std_out > out);

    std::string buf;
    std::copy(std::istreambuf_iterator<char>(out), {}, std::back_inserter(buf));

    c.wait();
    if (c.exit_code() != 0) {
      return absl::InternalError(
          absl::StrCat("ip exit status ", c.exit_code(), ":\n", buf));
    }

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
  } catch (const std::system_error& e) {
    return absl::InternalError(absl::StrCat("failed to run ip subprocess: ", e.what()));
  }
  return absl::NotFoundError(
      absl::StrCat("requested addrs not found: ", absl::StrJoin(ip_addrs, " ")));
}

}  // namespace heyp
