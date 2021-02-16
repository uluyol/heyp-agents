#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc != 3) {
    return 1;
  }
  if (strcmp(argv[1], "-t") != 0) {
    return 1;
  }
  if (strcmp(argv[2], "mangle") != 0) {
    return 1;
  }

  char buf[1024];
  for (int i = 0; i < 20; ++i) {
    memset(buf, 'A' + i, 1024);
    fwrite(buf, 1024, 1, stdout);
  }

  return 0;
}
