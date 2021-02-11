#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Taken from a VM
static const char output[] =
    "[{\"kind\":\"noqueue\",\"handle\":\"0:\",\"dev\":\"lo\",\"root\":true,"
    "\"refcnt\":2,\"options\":{}},{\"kind\":\"fq_codel\",\"handle\":\"0:\","
    "\"dev\":\"ens33\",\"root\":true,\"refcnt\":2,\"options\":{\"limit\":10240,"
    "\"flows\":1024,\"quantum\":1514,\"target\":4999,\"interval\":99999,"
    "\"memory_limit\":33554432,\"ecn\":true}}]";

int main(int argc, char **argv) {
  if (argc != 4) {
    return 1;
  }
  if (strcmp(argv[1], "-j") != 0) {
    return 1;
  }
  if (strcmp(argv[2], "qdisc") != 0) {
    return 1;
  }
  if (strcmp(argv[3], "list") != 0) {
    return 1;
  }
  fwrite(output, strlen(output), 1, stdout);

  return 0;
}
