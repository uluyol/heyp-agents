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

std::vector<double> FracTimeInEachStage(const proto::TestLopriClientConfig& config) {
  auto total_dur = absl::ZeroDuration();
  std::vector<absl::Duration> durs;
  for (auto s : config.workload_stages()) {
    absl::Duration d;
    if (!absl::ParseDuration(s.run_dur(), &d)) {
      std::cerr << "bad duration '" << s.run_dur() << "'";
      exit(2);
    }
    total_dur += d;
    durs.push_back(d);
  }

  std::vector<double> fracs;
  double accum = 0;
  for (auto d : durs) {
    accum += absl::ToDoubleSeconds(d) / absl::ToDoubleSeconds(total_dur);
    fracs.push_back(accum);
  }
  fracs.back() = 1;

  return fracs;
}

int64_t PickRandomInterarrival(const proto::TestLopriClientConfig& config,
                               const std::vector<double>& fracs,
                               absl::InsecureBitGen& rng) {
  int index = std::lower_bound(fracs.begin(), fracs.end(), absl::Uniform(rng, 0, 1)) -
              fracs.begin();

  auto stage = config.workload_stages(index);
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
      std::cerr << "unsupported interarrival distribution: " << stage.interarrival_dist()
                << "\n";
      exit(2);
  }

  double wait_sec = 0;
  switch (stage.interarrival_dist()) {
    case proto::DIST_CONSTANT:
      wait_sec = dist_param;
      break;
    case proto::DIST_UNIFORM:
      wait_sec = absl::Uniform(rng, 0, 2 * dist_param);
      break;
    case proto::DIST_EXPONENTIAL:
      wait_sec = absl::Exponential(rng, dist_param);
      break;
    default:
      std::cerr << "unreachable\n";
      return 3;
  }
  return wait_sec * 1'000'000'000;  // to ns
}

int Run(const proto::TestLopriClientConfig& config) {
  absl::InsecureBitGen rng;
  std::vector<double> fracs = FracTimeInEachStage(config);

  constexpr int kNumSamples = 1000;
  std::array<int64_t, kNumSamples> data;
  for (int i = 0; i < kNumSamples; ++i) {
    data[i] = PickRandomInterarrival(config, fracs, rng);
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