#include "heyp/posix/strerror.h"

#include "third_party/chromium/safe_strerror.h"

namespace heyp {

std::string StrError(int error_number) {
  return ::chromium::base::safe_strerror(error_number);
}

}  // namespace heyp
