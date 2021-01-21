#ifndef HEYP_THREADS_WAITGROUP_H_
#define HEYP_THREADS_WAITGROUP_H_

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace heyp {

class WaitGroup {
 public:
  void Add(int n = 1);
  void Done(int n = 1);

  void Wait();

 private:
  absl::Mutex mu_;
  int count_ ABSL_GUARDED_BY(mu_) = 0;
};

}  // namespace heyp

#endif  // HEYP_THREADS_WAITGROUP_H_
