#include <unistd.h>

#include "absl/flags/flag.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"
#include "heyp/init/init.h"
#include "heyp/integration/host-agent-os-tester.h"
#include "heyp/log/logging.h"

ABSL_FLAG(std::string, logdir, "", "directory to write debug log files to, if present");
ABSL_FLAG(std::string, run_dur, "60s", "how much time the test should measure for");
ABSL_FLAG(std::string, step_dur, "2s", "how long a single step in the test should last");
ABSL_FLAG(int32_t, num_hosts, 4, "number of hosts to emulate");
ABSL_FLAG(int64_t, max_rate_limit_mbps, 100, "maximum rate limit (in Mbps)");
ABSL_FLAG(bool, ignore_instantaneous_usage, false,
          "ignore instantaneous usage reports when estimating usage bps");

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);
  absl::SetFlag(&FLAGS_logtostderr, 1);

  absl::Duration run_dur;
  absl::Duration step_dur;

  if (!absl::ParseDuration(absl::GetFlag(FLAGS_run_dur), &run_dur)) {
    std::cerr << "invalid duration for -run_dur:" << absl::GetFlag(FLAGS_run_dur) << "\n";
    return 2;
  }
  if (!absl::ParseDuration(absl::GetFlag(FLAGS_step_dur), &step_dur)) {
    std::cerr << "invalid duration for -step_dur:" << absl::GetFlag(FLAGS_step_dur)
              << "\n";
    return 2;
  }

  heyp::testing::HostAgentOSTester tester({
      .device = "lo",
      .log_dir = absl::GetFlag(FLAGS_logdir),
      .use_hipri = true,
      .run_dur = run_dur,
      .step_dur = step_dur,
      .num_hosts = absl::GetFlag(FLAGS_num_hosts),
      .max_rate_limit_bps = absl::GetFlag(FLAGS_max_rate_limit_mbps) * 1024 * 1024,
      .ignore_instantaneous_usage = absl::GetFlag(FLAGS_ignore_instantaneous_usage),
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