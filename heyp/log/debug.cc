#include "heyp/log/debug.h"

#include <sys/prctl.h>
#include <unistd.h>

#include "heyp/io/subprocess.h"

namespace heyp {

// Dump all stack traces using GDB (best effort).
// Based on https://stackoverflow.com/a/4732119
void GdbPrintAllStacks(spdlog::logger* logger) {
  std::string pid = std::to_string(getpid());
  char name_buf[1024];
  name_buf[readlink("/proc/self/exe", name_buf, 1023)] = 0;
  prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);

  SubProcess subproc(logger);
  subproc.SetProgram("gdb", {name_buf, "--batch", "-n", "-ex", "attach " + pid, "-ex",
                             "thread apply all bt"});
  subproc.SetChannelAction(CHAN_STDOUT, ACTION_DUPPARENT);
  subproc.SetChannelAction(CHAN_STDERR, ACTION_DUPPARENT);
  if (subproc.Start()) {
    subproc.Wait();
  }
}

}  // namespace heyp