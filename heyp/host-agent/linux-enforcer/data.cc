#include "heyp/host-agent/linux-enforcer/data.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "heyp/io/subprocess.h"
#include "heyp/log/spdlog.h"
#include "third_party/simdjson/simdjson.h"

namespace heyp {

absl::StatusOr<std::string> FindDeviceResponsibleFor(
    const std::vector<std::string>& ip_addrs, spdlog::logger* logger,
    const std::string& ip_bin_name) {
  SubProcess subproc(logger);
  subproc.SetProgram(ip_bin_name, {"-json", "addr"});
  subproc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  subproc.SetChannelAction(CHAN_STDERR, ACTION_PIPE);
  if (!subproc.Start()) {
    return absl::UnknownError("failed to run ip");
  }
  std::string got_stdout;
  std::string got_stderr;
  ExitStatus got = subproc.Communicate(nullptr, &got_stdout, &got_stderr);

  if (!got.ok()) {
    return absl::UnknownError(absl::StrCat("ip: wait status: ", got.wait_status(),
                                           " exit status: ", got.exit_status(),
                                           "; stderr:\n", got_stderr));
  }

  if (absl::StripAsciiWhitespace(got_stdout).empty()) {
    return absl::UnknownError("no ip output");
  }
  simdjson::dom::parser parser;
  auto result = parser.parse(got_stdout);
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

// Finds the device responsible the input ip addresses. Discovers this information on
// systems where there is no JSON output from the "ip" command. Less reliable since we're
// not 100% sure on the data format.
absl::StatusOr<std::string> FindDeviceResponsibleForLessReliable(
    const std::vector<std::string>& ip_addrs, spdlog::logger* logger,
    const std::string& ip_bin_name) {
  SubProcess subproc(logger);
  subproc.SetProgram(ip_bin_name, {"-oneline", "addr"});
  subproc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  subproc.SetChannelAction(CHAN_STDERR, ACTION_PIPE);
  if (!subproc.Start()) {
    return absl::UnknownError("failed to run ip");
  }
  std::string got_stdout;
  std::string got_stderr;
  ExitStatus got = subproc.Communicate(nullptr, &got_stdout, &got_stderr);

  if (!got.ok()) {
    return absl::UnknownError(absl::StrCat("ip: wait status: ", got.wait_status(),
                                           " exit status: ", got.exit_status(),
                                           "; stderr:\n", got_stderr));
  }

  if (absl::StripAsciiWhitespace(got_stdout).empty()) {
    return absl::UnknownError("no ip output");
  }
  std::vector<std::string_view> lines = absl::StrSplit(got_stdout, '\n');
  for (std::string_view line : lines) {
    int field_n = 0;
    std::string_view iface = "";
    bool next_is_ip = false;
    absl::flat_hash_set<std::string> want_addrs{ip_addrs.begin(), ip_addrs.end()};
    for (std::string_view field :
         absl::StrSplit(line, absl::ByAnyChar(" \t"), absl::SkipEmpty())) {
      if (field_n == 1) {
        iface = field;
      }
      if (next_is_ip) {
        std::vector<std::string_view> t = absl::StrSplit(field, '/');
        std::string_view ip = t[0];
        want_addrs.erase(ip);
        next_is_ip = false;
      }
      if (field == "inet" || field == "inet6") {
        next_is_ip = true;
      }
      ++field_n;
    }

    if (want_addrs.empty()) {
      return std::string(iface);
    }
  }

  return absl::NotFoundError(
      absl::StrCat("requested addrs not found: ", absl::StrJoin(ip_addrs, " ")));
}

}  // namespace heyp
