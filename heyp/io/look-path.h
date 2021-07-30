#ifndef HEYP_IO_LOOK_PATH_H_
#define HEYP_IO_LOOK_PATH_H_

#include <string>

#include "absl/strings/string_view.h"

namespace heyp {

std::string LookPath(absl::string_view path);

}

#endif  // HEYP_IO_LOOK_PATH_H_
