#include "heyp/host-agent/linux-enforcer/tc-caller.h"

#include <algorithm>
#include <istream>
#include <iterator>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "heyp/io/subprocess.h"
#include "heyp/log/logging.h"
#include "third_party/simdjson/simdjson.h"

namespace heyp {

TcCaller::TcCaller(const std::string& tc_name) : tc_name_(tc_name) {}

absl::Status TcCaller::Batch(const absl::Cord& input, bool force) {
  std::vector<std::string> args{"-batch", "-", "-force"};
  if (!force) {
    args.resize(1);
  }

  auto result = RunSubprocess(tc_name_, args, input);
  if (!result.ok()) {
    return result.ErrorWhenRunning("tc -batch");
  }
  return absl::OkStatus();
}

absl::Status TcCaller::Call(const std::vector<std::string>& tc_args,
                            bool parse_into_json) {
  auto result = RunSubprocess(tc_name_, tc_args);
  if (!result.ok()) {
    return result.ErrorWhenRunning("tc");
  }

  buf_ = std::string(result.out);

  if (absl::StripAsciiWhitespace(buf_).empty() || !parse_into_json) {
    result_.reset();
  } else {
    auto result = parser_.parse(buf_);
    if (result.error() != simdjson::SUCCESS) {
      return absl::InternalError(absl::StrCat("failed to parse tc output to json: ",
                                              simdjson::error_message(result.error())));
    }
    result_ = result.value();
  }

  return absl::OkStatus();
}

}  // namespace heyp
