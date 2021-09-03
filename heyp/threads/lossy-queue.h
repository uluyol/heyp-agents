#ifndef HEYP_THREADS_LOSSY_QUEUE_H_
#define HEYP_THREADS_LOSSY_QUEUE_H_

#include <optional>

#include "absl/base/macros.h"
#include "absl/synchronization/mutex.h"

namespace heyp {

template <typename T>
class LossyQueue {
 public:
  LossyQueue<T>() {}

  LossyQueue(const LossyQueue<T>&) = delete;
  LossyQueue<T>& operator=(const LossyQueue<T>&) = delete;

  std::optional<T> Read();
  void Write(T data);
  void Close();

 private:
  absl::Mutex mu_;
  T data_ = T{};
  bool has_data_ = false;
  bool closed_ = false;
};

template <typename T>
std::optional<T> LossyQueue<T>::Read() {
  mu_.LockWhen(absl::Condition(
      +[](LossyQueue<T>* self) { return self->has_data_ || self->closed_; }, this));
  if (has_data_) {
    has_data_ = false;
    T data = std::move(data_);
    mu_.Unlock();
    return data;
  }
  ABSL_ASSERT(closed_);
  mu_.Unlock();
  return std::nullopt;
}

template <typename T>
void LossyQueue<T>::Write(T data) {
  absl::MutexLock l(&mu_);
  if (closed_) {
    return;
  }
  has_data_ = true;
  data_ = std::move(data);
}

template <typename T>
void LossyQueue<T>::Close() {
  absl::MutexLock l(&mu_);
  closed_ = true;
}

}  // namespace heyp

#endif  // HEYP_THREADS_LOSSY_QUEUE_H_
