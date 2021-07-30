#include "heyp/io/look-path.h"

int main() {
  if (std::string got = heyp::LookPath("sh"); got != "/bin/sh") {
    fprintf(stderr, "got %s, want /bin/sh\n", got.c_str());
    return 3;
  }
  return 0;
}
