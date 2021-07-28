#ifndef HEYP_POSIX_PIDFILE_H_
#define HEYP_POSIX_PIDFILE_H_

#include <string>

#include "absl/status/status.h"

namespace heyp {

absl::Status WritePidFile(const std::string& path);

}

#endif  // HEYP_POSIX_PIDFILE_H_
