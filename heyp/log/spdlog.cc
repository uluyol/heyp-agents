#include "heyp/log/spdlog.h"

#include <iostream>
#include <string>

#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_format.h"
#include "spdlog/pattern_formatter.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_sinks.h"

ABSL_FLAG(std::string, spdlog_file, "", "if specified, will log here instead to stderr");

namespace heyp {

static std::vector<spdlog::sink_ptr>* CreateSinks() {
  auto sinks = new std::vector<spdlog::sink_ptr>(0, nullptr);
  std::string path = absl::GetFlag(FLAGS_spdlog_file);
  if (path == "") {
    sinks->push_back(std::make_shared<spdlog::sinks::stderr_sink_mt>());
  } else {
    sinks->push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, true));
  }
  spdlog::flush_every(std::chrono::seconds(2));
  return sinks;
}

const std::vector<spdlog::sink_ptr>& LogSinks() {
  static std::vector<spdlog::sink_ptr>* sinks = CreateSinks();
  return *sinks;
}

spdlog::logger MakeLogger(std::string name) {
  auto sinks = LogSinks();
  auto logger = spdlog::logger(std::move(name), sinks.begin(), sinks.end());
  logger.set_formatter(std::make_unique<spdlog::pattern_formatter>(
      "T=%t %+", spdlog::pattern_time_type::utc));
  return logger;
}

void DumpStackTraceAndExit(int exit_status) {
  void* stack[32];
  int num_frame = absl::GetStackTrace(stack, 32, 1);
  for (int i = 0; i < num_frame; i++) {
    char tmp[1024];
    const char* symbol = "(unknown)";
    if (absl::Symbolize(stack[i], tmp, sizeof(tmp))) {
      symbol = tmp;
    }
    absl::FPrintF(stderr, "%p %s\n", stack[i], symbol);
  }
  exit(exit_status);
}

}  // namespace heyp
