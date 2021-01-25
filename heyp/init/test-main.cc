#include "gtest/gtest.h"
#include "heyp/init/init.h"

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  heyp::MainInit(&argc, &argv);
  return RUN_ALL_TESTS();
}
