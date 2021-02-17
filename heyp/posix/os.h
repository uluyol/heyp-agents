#ifndef HEYP_POSIX_OS_H_
#define HEYP_POSIX_OS_H_

namespace heyp {

#if __linux__
inline bool kHostIsLinux = true;
#else
inline bool kHostIsLinux = false;
#endif

}  // namespace heyp

#endif  // HEYP_POSIX_OS_H_
