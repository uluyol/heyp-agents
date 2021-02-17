#include "heyp/threads/executor.h"

#include "absl/memory/memory.h"
#include "glog/logging.h"

namespace heyp {

Executor::Executor(int num_workers) {
  workers_.reserve(num_workers);
  workers_.push_back(std::thread([this] {
    bool is_dead = false;
    std::function<void()> next_fn;
    while (!is_dead) {
      // Wait for data
      mu_.LockWhen(absl::Condition(
          +[](Status* st) -> bool { return (!st->tasks.empty()) || st->is_dead; },
          &this->st_));
      // Read data
      is_dead = this->st_.is_dead;
      if (!this->st_.tasks.empty()) {
        next_fn = this->st_.tasks.back();
        this->st_.tasks.pop_back();
      }
      mu_.Unlock();

      // Execute or exit.
      if (next_fn != nullptr) {
        next_fn();
        next_fn = nullptr;
      } else {
        ABSL_ASSERT(is_dead);
      }
    }
  }));
}

Executor::~Executor() {
  children_.Wait();
  {
    absl::MutexLock l(&mu_);
    st_.is_dead = true;
  }
  for (std::thread& t : workers_) {
    t.join();
  }
}

std::unique_ptr<TaskGroup> Executor::NewTaskGroup() {
  auto group = absl::WrapUnique(new TaskGroup(this));
  children_.Add();
  return group;
}

TaskGroup::~TaskGroup() {
  CHECK_EQ(exec_, static_cast<Executor*>(nullptr))
      << "must call WaitAll on TaskGroup before destroying";
}

void TaskGroup::WaitAll() {
  wg_.Wait();
  exec_->children_.Done();
  exec_ = nullptr;
}

void TaskGroup::AddTask(const std::function<void()>& fn) {
  wg_.Add();
  absl::MutexLock l(&exec_->mu_);
  ABSL_ASSERT(!exec_->st_.is_dead);
  exec_->st_.tasks.push_back([fn, this] {
    fn();
    this->wg_.Done();
  });
}

}  // namespace heyp
