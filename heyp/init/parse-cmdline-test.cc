#include <cstdio>

#include "absl/flags/flag.h"
#include "absl/strings/str_format.h"
#include "heyp/init/init.h"

ABSL_FLAG(std::string, flagval, "flagval", "usage info");

int main() {
  const char* test_args[] = {"parse_cmdline_test", "-flagval", "xyz1Z", "arg1", "arg2"};
  int test_argc = 5;

  char** got_argv = const_cast<char**>(test_args);

  heyp::MainInit(&test_argc, &got_argv);

  if (test_argc != 3) {
    absl::FPrintF(stderr, "want 3 args, got %d\n", test_argc);
    for (int i = 0; i < test_argc; ++i) {
      absl::FPrintF(stderr, "argv[%d] = %s\n", i, got_argv[i]);
    }
    return 2;
  }
  bool all_good = true;
  if (strcmp(got_argv[0], "parse_cmdline_test") != 0) {
    absl::FPrintF(stderr, "argv[0]: want parse_cmdline_test, got %s\n", got_argv[0]);
    all_good = false;
  }
  if (strcmp(got_argv[1], "arg1") != 0) {
    absl::FPrintF(stderr, "argv[1]: want arg1, got %s\n", got_argv[1]);
    all_good = false;
  }
  if (strcmp(got_argv[2], "arg2") != 0) {
    absl::FPrintF(stderr, "argv[2]: want arg2, got %s\n", got_argv[2]);
    all_good = false;
  }
  if (!all_good) {
    return 3;
  }
  return 0;
}
