#include "heyp/threads/set-name.h"

#include <pthread.h>

namespace heyp {

void SetThreadName(pthread_t handle, const char* name) {
#ifdef __linux__
  pthread_setname_np(handle, name);
#endif
}

void SetCurThreadName(const char* name) { SetThreadName(pthread_self(), name); }

}  // namespace heyp
