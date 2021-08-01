#include "heyp/threads/executor.h"

#include "absl/memory/memory.h"
#include "heyp/log/spdlog.h"

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
  H_ASSERT_EQ_MESG(exec_, static_cast<Executor*>(nullptr),
                   "must call WaitAll on TaskGroup before destroying");
}

absl::Status TaskGroup::WaitAll() {
  mu_.LockWhen(absl::Condition(
      +[](int* c) { return (*c) == 0; }, &count_));
  // done
  absl::Status st = status_;
  mu_.Unlock();
  exec_->children_.Done();
  exec_ = nullptr;
  return st;
}

void TaskGroup::WaitAllNoStatus() {
  auto st = WaitAll();
  H_ASSERT_MESG(st.ok(),
                "got non-OK status in WaitAllNoStatus; consider using WaitAll instead");
}

void TaskGroup::AddTask(const std::function<absl::Status()>& fn) {
  {
    absl::MutexLock l(&mu_);
    count_++;
  }
  absl::MutexLock l(&exec_->mu_);
  ABSL_ASSERT(!exec_->st_.is_dead);
  exec_->st_.tasks.push_back([fn, this] {
    auto st = fn();

    absl::MutexLock l(&this->mu_);
    this->status_.Update(st);
    ABSL_ASSERT(this->count_ > 0);
    this->count_--;
  });
}

void TaskGroup::AddTaskNoStatus(const std::function<void()>& fn) {
  {
    absl::MutexLock l(&mu_);
    count_++;
  }
  absl::MutexLock l(&exec_->mu_);
  ABSL_ASSERT(!exec_->st_.is_dead);
  exec_->st_.tasks.push_back([fn, this] {
    fn();

    absl::MutexLock l(&this->mu_);
    ABSL_ASSERT(this->count_ > 0);
    this->count_--;
  });
}

}  // namespace heyp
