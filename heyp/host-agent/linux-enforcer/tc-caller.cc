#include "heyp/host-agent/linux-enforcer/tc-caller.h"

#include <algorithm>
#include <istream>
#include <iterator>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "heyp/io/subprocess.h"
#include "heyp/log/spdlog.h"
#include "third_party/simdjson/simdjson.h"

namespace heyp {
namespace {
constexpr absl::Duration kTcTimeout = absl::Seconds(2);
}

TcCaller::TcCaller(spdlog::logger* logger, const std::string& tc_name)
    : tc_name_(tc_name), logger_(logger) {}

absl::Status TcCaller::Batch(const absl::Cord& input, bool force) {
  std::vector<std::string> args{"-batch", "-", "-force"};
  if (!force) {
    args.resize(1);
  }

  SubProcess subproc(logger_);
  subproc.SetProgram(tc_name_, args);
  subproc.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
  subproc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  subproc.SetChannelAction(CHAN_STDERR, ACTION_PIPE);
  if (!subproc.Start()) {
    return absl::UnknownError("failed to run tc -batch");
  }
  subproc.KillAfter(kTcTimeout);
  std::string for_stdin(input);
  std::string got_stdout;
  std::string got_stderr;
  int exit_status = subproc.Communicate(&for_stdin, &got_stdout, &got_stderr);
  if (exit_status != 0) {
    return absl::UnknownError(
        absl::StrCat("tc -batch: exit status ", exit_status, "; stderr:\n", got_stderr));
  }
  return absl::OkStatus();
}

absl::Status TcCaller::Call(const std::vector<std::string>& tc_args,
                            bool parse_into_json) {
  SubProcess subproc(logger_);
  subproc.SetProgram(tc_name_, tc_args);
  subproc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  subproc.SetChannelAction(CHAN_STDERR, ACTION_PIPE);
  if (!subproc.Start()) {
    return absl::UnknownError("failed to run tc");
  }
  subproc.KillAfter(kTcTimeout);
  buf_.clear();
  std::string got_stderr;
  int exit_status = subproc.Communicate(nullptr, &buf_, &got_stderr);

  if (exit_status != 0) {
    return absl::UnknownError(
        absl::StrCat("tc: exit status ", exit_status, "; stderr:\n", got_stderr));
  }

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
