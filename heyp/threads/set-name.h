#ifndef HEYP_THREADS_SET_NAME_H_
#define HEYP_THREADS_SET_NAME_H_

#include <pthread.h>

namespace heyp {

void SetCurThreadName(const char* name);
void SetThreadName(pthread_t handle, const char* name);

}  // namespace heyp

#endif  // HEYP_THREADS_SET_NAME_H_
