#include "heyp/init/init.h"

#include <cstdlib>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/parse.h"
#include "heyp/log/logging.h"

namespace heyp {

void MainInit(int* argc, char*** argv) {
  google::InitGoogleLogging((*argv)[0]);
  absl::InitializeSymbolizer((*argv)[0]);
  absl::InstallFailureSignalHandler(absl::FailureSignalHandlerOptions());
  std::vector<char*> updated = absl::ParseCommandLine(*argc, *argv);
  *argc = updated.size();
  char** new_argv = static_cast<char**>(calloc(*argc, sizeof(char*)));
  for (size_t i = 0; i < updated.size(); ++i) {
    new_argv[i] = updated[i];
  }
  *argv = new_argv;
}

}  // namespace heyp
