#include "heyp/threads/executor.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

template <int N>
class BusyWork {
 public:
  BusyWork(std::unique_ptr<TaskGroup> tg) : tg_(std::move(tg)) {
    auto tasksp = &tasks_;
    for (int i = 0; i < N; ++i) {
      tg_->AddTaskNoStatus([i, tasksp] { (*tasksp)[i].store(1); });
    }
  }

  void Wait() { tg_->WaitAllNoStatus(); }

  void CheckDone() {
    int sum = 0;
    for (int i = 0; i < N; ++i) {
      sum += tasks_[i].load();
    }
    EXPECT_EQ(sum, N);
  }

 private:
  std::unique_ptr<TaskGroup> tg_;
  std::array<std::atomic<int>, N> tasks_;
};

TEST(ExecutorTest, OneTaskGroup) {
  constexpr int kNumTasks = 121;

  Executor executor(9);
  BusyWork<kNumTasks> work(executor.NewTaskGroup());
  work.Wait();
  work.CheckDone();
}

TEST(ExecutorTest, ReturnsError) {
  Executor executor(3);
  auto tg = executor.NewTaskGroup();
  tg->AddTask([] { return absl::OkStatus(); });
  tg->AddTask([] {
    absl::SleepFor(absl::Milliseconds(50));
    return absl::FailedPreconditionError("");
  });
  tg->AddTask([] {
    absl::SleepFor(absl::Milliseconds(100));
    return absl::OkStatus();
  });
  EXPECT_THAT(tg->WaitAll().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(ExecutorTest, MultipleTaskGroups) {
  constexpr int kNumTasks1 = 10;
  constexpr int kNumTasks2 = 21;

  Executor executor(3);

  BusyWork<kNumTasks1> work1(executor.NewTaskGroup());
  BusyWork<kNumTasks2> work2(executor.NewTaskGroup());
  BusyWork<kNumTasks1> work3(executor.NewTaskGroup());
  BusyWork<kNumTasks2> work4(executor.NewTaskGroup());
  BusyWork<kNumTasks1> work5(executor.NewTaskGroup());
  BusyWork<kNumTasks2> work6(executor.NewTaskGroup());

  work3.Wait();
  work3.CheckDone();

  work2.Wait();
  work4.Wait();

  work2.CheckDone();
  work4.CheckDone();

  work1.Wait();
  work5.Wait();

  work5.CheckDone();
  work1.CheckDone();

  work6.Wait();
  work6.CheckDone();
}

}  // namespace
}  // namespace heyp
