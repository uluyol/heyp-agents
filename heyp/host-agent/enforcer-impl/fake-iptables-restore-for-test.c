#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_NEXT_ARG_IS(s)            \
  if (strcmp(argv[cur_arg++], s) != 0) { \
    return 1;                            \
  }

int main(int argc, char **argv) {
  if (argc != 9) {
    return 1;
  }
  int cur_arg = 1;
  ASSERT_NEXT_ARG_IS("-w");
  ASSERT_NEXT_ARG_IS("5");
  ASSERT_NEXT_ARG_IS("-W");
  ASSERT_NEXT_ARG_IS("100000");
  ASSERT_NEXT_ARG_IS("-T");
  ASSERT_NEXT_ARG_IS("mangle");
  ASSERT_NEXT_ARG_IS("--noflush");
  ASSERT_NEXT_ARG_IS("--counters");

  char buf[1024];
  for (int i = 0; i < 20; ++i) {
    if (fread(buf, 1, 1024, stdin) != 1024) {
      return 2;
    }
    for (int j = 0; j < 1024; ++j) {
      if (buf[j] != 'Z' - i) {
        return 3;
      }
    }
  }

  return 0;
}
