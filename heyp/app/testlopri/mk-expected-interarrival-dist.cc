#include <algorithm>
#include <array>
#include <cstdint>

#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "gflags/gflags.h"
#include "heyp/init/init.h"
#include "heyp/proto/app.pb.h"
#include "heyp/proto/fileio.h"

namespace heyp {
namespace {

int Run(const proto::TestLopriClientConfig& config) {
  double rpcs_per_sec = config.target_average_bps() / (8 * config.rpc_size_bytes());

  double dist_param = 0;
  switch (config.interarrival_dist()) {
    case proto::Distribution::CONSTANT:
      dist_param = 1.0 / rpcs_per_sec;
      break;
    case proto::Distribution::UNIFORM:
      dist_param = 1.0 / rpcs_per_sec;
      break;
    case proto::Distribution::EXPONENTIAL:
      dist_param = rpcs_per_sec;
      break;
    default:
      std::cerr << "unsupported interarrival distribution: " << config.interarrival_dist()
                << "\n";
      return 2;
  }

  absl::InsecureBitGen rng;

  constexpr int kNumSamples = 1000;
  std::array<int64_t, kNumSamples> data;
  for (int i = 0; i < kNumSamples; ++i) {
    double wait_sec = 0;
    switch (config.interarrival_dist()) {
      case proto::Distribution::CONSTANT:
        wait_sec = dist_param;
        break;
      case proto::Distribution::UNIFORM:
        wait_sec = absl::Uniform(rng, 0, 2 * dist_param);
        break;
      case proto::Distribution::EXPONENTIAL:
        wait_sec = absl::Exponential(rng, dist_param);
        break;
      default:
        std::cerr << "unreachable\n";
        return 3;
    }
    data[i] = wait_sec * 1'000'000'000;  // to ns
  }

  std::sort(data.begin(), data.end());

  absl::FPrintF(stdout, "Percentile,Value,NumSamples\n");
  for (int i = 0; i < data.size(); ++i) {
    double perc = 100 * (i + 1);
    perc = perc / data.size();
    absl::FPrintF(stdout, "%f,%d,1\n", perc, data[i]);
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