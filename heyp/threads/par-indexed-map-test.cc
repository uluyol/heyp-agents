#include "heyp/threads/par-indexed-map.h"

#include <string>
#include <unordered_map>

#include "absl/container/flat_hash_map.h"
#include "absl/random/random.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace heyp {
namespace {

TEST(ParIndexedMapTest, BasicSequential) {
  ParIndexedMap<std::string, double, std::unordered_map<std::string, ParID>> parmap;

  ASSERT_EQ(parmap.GetID("c"), 0);
  ASSERT_EQ(parmap.GetID("b"), 1);
  ASSERT_EQ(parmap.GetID("a"), 2);
  ASSERT_EQ(parmap.GetID("c"), 0);

  parmap.OnID(1, [](double& v) { v = 55; });
  parmap.OnID(2, [](double& v) { v = 33; });
  parmap.OnID(0, [](double& v) { v = 66; });

  int times_called = 0;
  ASSERT_EQ(parmap.NumIDs(), 3);
  parmap.ForEach(0, 3, [&times_called](ParID id, double& val) {
    switch (times_called++) {
      case 0: {
        EXPECT_EQ(val, 66);
        return;
      }
      case 1: {
        EXPECT_EQ(val, 55);
        return;
      }
      case 2: {
        EXPECT_EQ(val, 33);
        return;
      }
    }
    FAIL();
  });

  EXPECT_EQ(times_called, 3);

  auto copymap = parmap.BestEffortCopy();

  // No concurrent ops -> copymap is a true copy
  times_called = 0;
  ASSERT_EQ(copymap->NumIDs(), 3);
  copymap->ForEach(0, 3, [&times_called](ParID id, double& val) {
    switch (times_called++) {
      case 0: {
        EXPECT_EQ(val, 66);
        return;
      }
      case 1: {
        EXPECT_EQ(val, 55);
        return;
      }
      case 2: {
        EXPECT_EQ(val, 33);
        return;
      }
    }
    FAIL();
  });

  EXPECT_EQ(times_called, 3);
}

TEST(ParIndexedMapTest, MultiSpan) {
  ParIndexedMap<int64_t, int64_t, absl::flat_hash_map<int64_t, ParID>> parmap;

  for (int i = 0; i < 3001; ++i) {
    ParID id = parmap.GetID(i);
    ASSERT_EQ(id, i);
    parmap.OnID(id, [i](int64_t& val) { val = i; });
  }

  EXPECT_EQ(parmap.NumIDs(), 3001);
  parmap.ForEach(0, 3001, [](ParID id, int64_t val) { EXPECT_EQ(id, val); });
  parmap.ForEach(999, 1001, [](ParID id, int64_t val) { EXPECT_EQ(id, val); });
}

TEST(ParIndexedMapTest, ParWrites) {
  ParIndexedMap<int64_t, int64_t, absl::flat_hash_map<int64_t, ParID>> parmap;

  for (int i = 0; i < 3001; ++i) {
    ParID id = parmap.GetID(i);
    ASSERT_EQ(id, i);
    parmap.OnID(id, [](int64_t& val) { val = 55; });
  }

  std::atomic<bool> finish(false);

  std::thread adder([&] {
    absl::InsecureBitGen gen;
    while (!finish.load()) {
      int64_t i = absl::Uniform<int64_t>(gen, 0, 5000);
      parmap.GetID(i);
    }
  });

  std::thread writer([&] {
    absl::InsecureBitGen gen;
    while (!finish.load()) {
      ParID id = absl::Uniform<ParID>(gen, 0, 3001);
      parmap.OnID(id, [](int64_t& v) { ++v; });
    }
  });

  EXPECT_GE(parmap.NumIDs(), 3001);
  parmap.ForEach(0, 3001, [](ParID id, int64_t val) { EXPECT_GE(val, 55); });
  EXPECT_GE(parmap.NumIDs(), 3001);
  parmap.ForEach(0, 3001, [](ParID id, int64_t val) { EXPECT_GE(val, 55); });
  EXPECT_GE(parmap.NumIDs(), 3001);
  parmap.ForEach(0, 3001, [](ParID id, int64_t val) { EXPECT_GE(val, 55); });
  finish.store(true);

  writer.join();
  adder.join();

  ParID end_id = parmap.NumIDs();
  parmap.ForEach(3001, end_id, [](ParID id, int64_t val) { EXPECT_EQ(val, 0); });
}

}  // namespace
}  // namespace heyp
