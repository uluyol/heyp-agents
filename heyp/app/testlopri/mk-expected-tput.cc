#include <algorithm>
#include <array>
#include <cstdint>

#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "gflags/gflags.h"
#include "heyp/init/init.h"
#include "heyp/proto/app.pb.h"
#include "heyp/proto/fileio.h"

namespace heyp {
namespace {

int Run(const proto::TestLopriClientConfig& config) {
  absl::InsecureBitGen rng;

  std::vector<std::pair<absl::Duration, uint64_t>> time_and_count{
      {absl::ZeroDuration(), 0}};
  absl::Duration stage_end_time = absl::ZeroDuration();
  absl::Duration now = absl::ZeroDuration();
  for (const auto& stage : config.workload_stages()) {
    {
      absl::Duration d;
      if (!absl::ParseDuration(stage.run_dur(), &d)) {
        std::cerr << "bad duration '" << stage.run_dur() << "'";
        exit(2);
      }
      stage_end_time += d;
    }

    double rpcs_per_sec = stage.target_average_bps() / (8 * stage.rpc_size_bytes());
    double dist_param = 0;
    switch (stage.interarrival_dist()) {
      case proto::DIST_CONSTANT:
        dist_param = 1.0 / rpcs_per_sec;
        break;
      case proto::DIST_UNIFORM:
        dist_param = 1.0 / rpcs_per_sec;
        break;
      case proto::DIST_EXPONENTIAL:
        dist_param = rpcs_per_sec;
        break;
      default:
        std::cerr << "unsupported interarrival distribution: "
                  << stage.interarrival_dist() << "\n";
        exit(2);
    }

    while (now < stage_end_time) {
      switch (stage.interarrival_dist()) {
        case proto::DIST_CONSTANT:
          now += absl::Nanoseconds(dist_param * 1e9);
          break;
        case proto::DIST_UNIFORM:
          now += absl::Nanoseconds(absl::Uniform(rng, 0, 2 * dist_param) * 1e9);
          break;
        case proto::DIST_EXPONENTIAL:
          now += absl::Nanoseconds(absl::Exponential(rng, dist_param) * 1e9);
          break;
        default:
          std::cerr << "unreachable\n";
          return 3;
      }

      while (now > time_and_count.back().first + absl::Milliseconds(1)) {
        auto last = time_and_count.back();
        time_and_count.push_back({last.first + absl::Milliseconds(1), 0});
      }
      time_and_count.back().second++;
    }
  }

  for (std::pair<absl::Duration, uint64_t> tc_pair : time_and_count) {
    absl::FPrintF(stdout, "%d,%d\n", absl::ToInt64Nanoseconds(tc_pair.first),
                  tc_pair.second);
  }

  return 0;
}

}  // namespace
}  // namespace heyp

DEFINE_string(c, "testlopri.textproto", "path to config file");

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);

  heyp::proto::TestLopriClientConfig c;
  if (!heyp::ReadTextProtoFromFile(FLAGS_c, &c)) {
    absl::FPrintF(stderr, "failed to read config file '%s'\n", FLAGS_c);
    return 3;
  }

  return heyp::Run(c);
}
