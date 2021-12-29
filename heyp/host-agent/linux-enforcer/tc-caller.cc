#include "heyp/host-agent/linux-enforcer/tc-caller.h"

#include <algorithm>
#include <istream>
#include <iterator>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "heyp/io/subprocess.h"
#include "heyp/log/spdlog.h"
#include "third_party/simdjson/simdjson.h"

namespace heyp {
namespace {
constexpr absl::Duration kTcTimeout = absl::Seconds(2);
constexpr bool kDebugDecomposeBatch = false;

// DecomposeBatch converts the input into a sequence of commands that should be run.
// It is meant for debugging and is not particularly efficient.
std::vector<std::vector<std::string>> DecomposeBatch(const absl::Cord& input_cord) {
  std::string input(input_cord);
  std::vector<std::vector<std::string>> all_cmds;
  for (std::string_view line : absl::StrSplit(input, "\n")) {
    std::vector<std::string> args =
        absl::StrSplit(line, absl::ByAnyChar(" \t"), absl::SkipEmpty());
    if (args.empty()) {
      continue;
    }
    all_cmds.push_back(args);
  }
  return all_cmds;
}
}  // namespace

TcCaller::TcCaller(const std::string& tc_name) : tc_name_(tc_name), logger_(nullptr) {}

absl::Status TcCaller::Batch(const absl::Cord& input, bool force) {
  if (kDebugDecomposeBatch) {
    return DecomposeAndRunBatch(input, force);
  }

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
  ExitStatus got = subproc.Communicate(&for_stdin, &got_stdout, &got_stderr);
  if (!got.ok()) {
    return absl::UnknownError(absl::StrCat("tc -batch: wait status: ", got.wait_status(),
                                           " exit status: ", got.exit_status(),
                                           "; stderr:\n", got_stderr, "\ninput:\n",
                                           std::string(input)));
  }
  return absl::OkStatus();
}

absl::Status TcCaller::DecomposeAndRunBatch(const absl::Cord& input, bool force) {
  std::vector<std::vector<std::string>> cmds = DecomposeBatch(input);

  absl::Status ret = absl::OkStatus();
  for (auto cmd : cmds) {
    ret.Update(Call(cmd, false));
    if (!ret.ok() && !force) {
      break;
    }
  }
  return ret;
}

absl::Status TcCaller::Call(const std::vector<std::string>& tc_args,
                            bool parse_into_json) {
  SubProcess subproc(logger_);
  subproc.SetProgram(tc_name_, tc_args);
  subproc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  subproc.SetChannelAction(CHAN_STDERR, ACTION_PIPE);
  if (!subproc.Start()) {
    return absl::UnknownError("failed to start tc");
  }
  subproc.KillAfter(kTcTimeout);
  buf_.clear();
  std::string got_stderr;
  ExitStatus got = subproc.Communicate(nullptr, &buf_, &got_stderr);

  if (!got.ok()) {
    return absl::UnknownError(absl::StrCat(
        "tc ", absl::StrJoin(tc_args, " "), " wait status: ", got.wait_status(),
        " exit status: ", got.exit_status(), "; stderr:\n", got_stderr));
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
