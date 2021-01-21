#ifndef HEYP_THREADS_EXECUTOR_H_
#define HEYP_THREADS_EXECUTOR_H_

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "heyp/threads/waitgroup.h"

namespace heyp {

class TaskGroup;

class Executor {
 public:
  explicit Executor(int num_workers);
  ~Executor();

  std::unique_ptr<TaskGroup> NewTaskGroup();

 private:
  std::vector<std::thread> workers_;

  struct Status {
    std::vector<std::function<void()>> tasks;
    bool is_dead = false;
  };

  absl::Mutex mu_;
  Status st_ ABSL_GUARDED_BY(mu_);
  WaitGroup children_;

  friend class TaskGroup;
};

class TaskGroup {
 public:
  TaskGroup(const TaskGroup&) = delete;
  TaskGroup& operator=(const TaskGroup&) = delete;

  ~TaskGroup();

  void AddTask(const std::function<void()>& fn);
  void WaitAll();

 private:
  TaskGroup(Executor* e) : exec_(e) {}

  Executor* exec_ = nullptr;
  WaitGroup wg_;

  friend class Executor;
};

}  // namespace heyp

#endif  // HEYP_THREADS_EXECUTOR_H_
