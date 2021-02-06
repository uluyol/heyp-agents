#include <cstdint>
#include <cstring>
#include <iostream>

#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

static void PrintRec(std::vector<std::string> recs) {
  std::cout << absl::StrJoin(recs, " ") << "\n";
}

int main(int argc, char **argv) {
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "-E") == 0) {
      absl::SleepFor(absl::Milliseconds(40));
      PrintRec({
          "UNCONN",
          "1",
          "0",
          "140.197.113.99:22",
          "165.121.234.111:21364",
          "wscale:6,7",
          "rto:236",
          "rtt:33.49/1.669",
          "ato:40",
          "mss:1448",
          "pmtu:1500",
          "rcvmss:1392",
          "advmss:1448",
          "cwnd:10",
          "bytes_sent:4140",
          "bytes_acked:4141",
          "bytes_received:3302",
          "segs_out:21",
          "segs_in:31",
          "data_segs_out:14",
          "data_segs_in:13",
          "send 3458943bps",
          "lastsnd:72",
          "lastrcv:40",
          "pacing_rate"
          "6917808bps"
          "delivery_rate"
          "336408bps"
          "delivered:16",
          "busy:436ms",
          "rcv_space:14600",
          "rcv_ssthresh:64076",
          "minrtt:31.792",
      });
      std::cout.flush();
      while (true) {
        absl::SleepFor(absl::Seconds(3));
      }
      return 0;
    }
  }

  PrintRec({
      "UNCONN",
      "1",
      "0",
      "140.197.113.99:22",
      "165.121.234.111:21364",
      "wscale:6,7",
      "rto:236",
      "rtt:33.49/1.669",
      "ato:40",
      "mss:1448",
      "pmtu:1500",
      "rcvmss:1392",
      "advmss:1448",
      "cwnd:10",
      "bytes_sent:240",
      "bytes_acked:4141",
      "bytes_received:3302",
      "segs_out:21",
      "segs_in:31",
      "data_segs_out:14",
      "data_segs_in:13",
      "send 458943bps",
      "lastsnd:72",
      "lastrcv:40",
      "pacing_rate"
      "6917808bps"
      "delivery_rate"
      "336408bps"
      "delivered:16",
      "busy:436ms",
      "rcv_space:14600",
      "rcv_ssthresh:64076",
      "minrtt:31.792",
  });
  PrintRec({
      "UNCONN",
      "1",
      "0",
      "10.197.113.99:80",
      "165.121.234.111:21364",
      "wscale:6,7",
      "rto:236",
      "rtt:33.49/1.669",
      "ato:40",
      "mss:1448",
      "pmtu:1500",
      "rcvmss:1392",
      "advmss:1448",
      "cwnd:10",
      "bytes_sent:240",
      "bytes_acked:4141",
      "bytes_received:3302",
      "segs_out:21",
      "segs_in:31",
      "data_segs_out:14",
      "data_segs_in:13",
      "send 458943bps",
      "lastsnd:72",
      "lastrcv:40",
      "pacing_rate"
      "6917808bps"
      "delivery_rate"
      "336408bps"
      "delivered:16",
      "busy:436ms",
      "rcv_space:14600",
      "rcv_ssthresh:64076",
      "minrtt:31.792",
  });
  PrintRec({
      "UNCONN",
      "1",
      "0",
      "140.197.113.99:99",
      "165.121.234.111:21364",
      "wscale:6,7",
      "rto:236",
      "rtt:33.49/1.669",
      "ato:40",
      "mss:1448",
      "pmtu:1500",
      "rcvmss:1392",
      "advmss:1448",
      "cwnd:10",
      "bytes_sent:9999",
      "bytes_acked:4141",
      "bytes_received:3302",
      "segs_out:21",
      "segs_in:31",
      "data_segs_out:14",
      "data_segs_in:13",
      "send 999999999bps",
      "lastsnd:72",
      "lastrcv:40",
      "pacing_rate"
      "6917808bps"
      "delivery_rate"
      "336408bps"
      "delivered:16",
      "busy:436ms",
      "rcv_space:14600",
      "rcv_ssthresh:64076",
      "minrtt:31.792",
  });
  std::cout.flush();
  return 0;
}
