#include "heyp/threads/waitgroup.h"

namespace heyp {

void WaitGroup::Add(int n) {
  ABSL_ASSERT(n >= 0);
  absl::MutexLock l(&mu_);
  count_ += n;
}

void WaitGroup::Done(int n) {
  ABSL_ASSERT(n >= 0);
  absl::MutexLock l(&mu_);
  ABSL_ASSERT(count_ >= n);
  count_ -= n;
}

void WaitGroup::Wait() {
  mu_.LockWhen(absl::Condition(
      +[](int *c) { return (*c) == 0; }, &count_));
  // done
  mu_.Unlock();
}

}  // namespace heyp
