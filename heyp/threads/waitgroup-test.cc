#include "heyp/threads/waitgroup.h"

#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

TEST(WaitGroupTest, CountIsExpected) {
  constexpr int kNumTasks = 15;

  WaitGroup wg;
  std::atomic<int> n = 0;
  wg.Add(kNumTasks);
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumTasks; ++i) {
    threads.push_back(std::thread([&wg, &n, i] {
      n += i;
      wg.Done();
    }));
  }

  wg.Wait();
  EXPECT_EQ(n, ((kNumTasks - 1) * kNumTasks) / 2);

  for (int i = 0; i < kNumTasks; ++i) {
    threads[i].join();
  }
}

}  // namespace
}  // namespace heyp
