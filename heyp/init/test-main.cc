#include "glog/logging.h"
#include "gtest/gtest.h"
#include "heyp/init/init.h"

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  heyp::MainInit(&argc, &argv);
  FLAGS_logtostderr = 1;
  return RUN_ALL_TESTS();
}
