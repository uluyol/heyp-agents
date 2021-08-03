#include <stdio.h>
#include <unistd.h>

#include "absl/time/clock.h"
#include "absl/time/time.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s duration\n", argv[0]);
    return 2;
  }

  absl::Duration wait_dur;
  if (!absl::ParseDuration(argv[1], &wait_dur)) {
    fprintf(stderr, "invalid duration: %s\n", argv[0]);
    return 3;
  }

  fork();

  absl::Time freeze_point = absl::Now() + wait_dur;
  while (true) {
    int c = fgetc(stdin);
    if (c <= 0) {
      break;
    }
    fputc(static_cast<char>(c), stdout);
    fputc(static_cast<char>(c), stderr);

    absl::SleepFor(wait_dur / 10);

    if (absl::Now() > freeze_point) {
      while (true) {
        absl::SleepFor(absl::Seconds(1));
      }
    }
  }
  return 0;
}
