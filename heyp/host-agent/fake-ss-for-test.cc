#include <cstdint>
#include <cstring>
#include <iostream>

#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

static void PrintRec(std::vector<std::string> recs) {
  std::cout << absl::StrJoin(recs, " ") << "\n";
}

int main(int argc, char** argv) {
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
  PrintRec({
      "ESTAB",
      "0",
      "0",
      "[::ffff:140.197.113.99]:4580",
      "[::ffff:192.168.1.7]:38290",
      "bbr",
      "wscale:7,7",
      "rto:204",
      "rtt:0.128/0.085",
      "ato:40",
      "mss:1448",
      "pmtu:1500",
      "rcvmss:536",
      "advmss:1448",
      "cwnd:43",
      "bytes_sent:1431",
      "bytes_acked:1431",
      "bytes_received:2214",
      "segs_out:100",
      "segs_in:95",
      "data_segs_out:33",
      "data_segs_in:67",
      "bbr:(bw:413714088bps,mrtt:0.028,pacing_gain:2.88672,cwnd_gain:2.88672)",
      "send",
      "3891500000bps",
      "lastsnd:1536",
      "lastrcv:1096",
      "lastack:1096",
      "pacing_rate",
      "4355966600bps",
      "delivery_rate",
      "413714280bps",
      "delivered:34",
      "app_limited",
      "rcv_space:14600",
      "rcv_ssthresh:64076",
      "minrtt:0.028",
  });
  std::cout.flush();
  return 0;
}
