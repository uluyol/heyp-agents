#include "heyp/init/init.h"

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "gflags/gflags.h"
#include "heyp/log/logging.h"

namespace heyp {

void MainInit(int* argc, char*** argv) {
  google::InitGoogleLogging((*argv)[0]);
  absl::InitializeSymbolizer((*argv)[0]);
  absl::InstallFailureSignalHandler(absl::FailureSignalHandlerOptions());
  gflags::ParseCommandLineFlags(argc, argv, true);
}

}  // namespace heyp
