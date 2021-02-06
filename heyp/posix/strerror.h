#ifndef HEYP_POSIX_STRERROR_H_
#define HEYP_POSIX_STRERROR_H_

#include <string>

namespace heyp {

// StrError is a thread-safe, portable, C++ friendly variant of strerror.
std::string StrError(int error_number);

}  // namespace heyp

#endif  // HEYP_POSIX_STRERROR_H_
