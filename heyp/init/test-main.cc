#include "absl/flags/flag.h"
#include "gtest/gtest.h"
#include "heyp/init/init.h"
#include "ortools/base/logging.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  heyp::MainInit(&argc, &argv);
  absl::SetFlag(&FLAGS_logtostderr, 1);
  return RUN_ALL_TESTS();
}
