#ifndef HEYP_THREADS_MUTEX_HELPERS_H_
#define HEYP_THREADS_MUTEX_HELPERS_H_

#include <mutex>

#include "absl/base/thread_annotations.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "spdlog/spdlog.h"

namespace heyp {

// TimedMutex is a wrapper around std::timed_mutex that has thread annotations.
class ABSL_LOCKABLE TimedMutex {
 public:
  // TODO: contains only bare minimum needed for current usage with MutexLockWarnLong.
  // Consider extending to full std::timed_mutex API.

  bool TryLockFor(absl::Duration dur) ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) {
    return mu_.try_lock_for(absl::ToChronoNanoseconds(dur));
  }

  void Lock(
      absl::Duration long_dur, spdlog::logger* logger, absl::string_view lock_name,
      absl::FunctionRef<void(int count)> on_long = [](int count) {})
      ABSL_EXCLUSIVE_LOCK_FUNCTION() {
    int count = 0;
    while (!TryLockFor(long_dur)) {
      count++;
      SPDLOG_LOGGER_WARN(logger, "waiting to acquire {}: took more than {} (so far)",
                         lock_name, absl::FormatDuration(count * long_dur));
      on_long(count);
    }
  }

  void Unlock() ABSL_UNLOCK_FUNCTION() { mu_.unlock(); }

 private:
  std::timed_mutex mu_;
};

class ABSL_SCOPED_LOCKABLE MutexLockWarnLong {
 public:
  MutexLockWarnLong(
      TimedMutex* mu, absl::Duration long_dur, spdlog::logger* logger,
      absl::string_view lock_name,
      absl::FunctionRef<void(int count)> on_long = [](int count) {})
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : mu_(mu) {
    int count = 0;
    while (!mu_->TryLockFor(long_dur)) {
      count++;
      SPDLOG_LOGGER_WARN(logger, "waiting to acquire {}: took more than {} (so far)",
                         lock_name, absl::FormatDuration(count * long_dur));
      on_long(count);
    }
  }

  MutexLockWarnLong(TimedMutex* mu, absl::Duration long_dur, spdlog::logger* logger,
                    absl::string_view lock_name, absl::FunctionRef<void()> on_long)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(mu)
      : MutexLockWarnLong(mu, long_dur, logger, lock_name,
                          [on_long](int count) { on_long(); }) {}

  MutexLockWarnLong(const MutexLockWarnLong&) = delete;
  MutexLockWarnLong(MutexLockWarnLong&&) = delete;
  MutexLockWarnLong& operator=(const MutexLockWarnLong&) = delete;
  MutexLockWarnLong& operator=(MutexLockWarnLong&&) = delete;

  ~MutexLockWarnLong() ABSL_UNLOCK_FUNCTION() { mu_->Unlock(); }

 private:
  TimedMutex* mu_;
};

}  // namespace heyp

#endif  // HEYP_THREADS_MUTEX_HELPERS_H_
