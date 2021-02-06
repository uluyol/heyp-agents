#include <unistd.h>

#include "gflags/gflags.h"
#include "glog/logging.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"
#include "heyp/init/init.h"
#include "heyp/integration/host-agent-os-tester.h"

DEFINE_string(run_dur, "60s", "how much time the test should measure for");
DEFINE_string(step_dur, "2s", "how long a single step in the test should last");
DEFINE_int32(num_hosts, 4, "number of hosts to emulate");
DEFINE_int64(max_rate_limit_mbps, 100, "maximum rate limit (in Mbps)");

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);
  FLAGS_logtostderr = true;

  absl::Duration run_dur;
  absl::Duration step_dur;

  if (!absl::ParseDuration(FLAGS_run_dur, &run_dur)) {
    std::cerr << "invalid duration for -run_dur:" << FLAGS_run_dur << "\n";
    return 2;
  }
  if (!absl::ParseDuration(FLAGS_step_dur, &step_dur)) {
    std::cerr << "invalid duration for -step_dur:" << FLAGS_step_dur << "\n";
    return 2;
  }

  heyp::testing::HostAgentOSTester tester({
      .run_dur = run_dur,
      .step_dur = step_dur,
      .num_hosts = FLAGS_num_hosts,
      .max_rate_limit_bps = FLAGS_max_rate_limit_mbps * 1024 * 1024,
  });

  auto metrics_or = tester.Run();
  if (!metrics_or.ok()) {
    std::cerr << "failed to collect: " << metrics_or.status() << "\n";
    return 1;
  }

  LOG(INFO) << "run successful; printing metrics";

  {
    google::protobuf::io::FileOutputStream out(1);
    if (!google::protobuf::TextFormat::Print(*metrics_or, &out)) {
      std::cerr << "failed to print metrics\n";
      return 1;
    }
  }

  close(1);
  return 0;
}