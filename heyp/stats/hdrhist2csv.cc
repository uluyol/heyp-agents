#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "absl/strings/str_format.h"
#include "gflags/gflags.h"
#include "heyp/init/init.h"
#include "heyp/proto/fileio.h"
#include "heyp/stats/hdrhistogram.h"

DEFINE_bool(header, true, "if set (default), output a header for the CSV");

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);

  std::vector<std::string> inputs;
  inputs.reserve(std::max(1, argc - 1));
  if (argc == 1) {
    inputs.push_back("/dev/stdin");
  }
  for (int i = 1; i < argc; ++i) {
    inputs.push_back(std::string(argv[1]));
  }

  // Pass 1: Load all hists and determine a config that accomodates all values

  heyp::proto::HdrHistogram::Config hist_config;
  hist_config.set_lowest_discernible_value(std::numeric_limits<int64_t>::max());
  std::vector<heyp::proto::HdrHistogram> proto_hists(inputs.size(),
                                                     heyp::proto::HdrHistogram{});
  for (size_t i = 0; i < inputs.size(); ++i) {
    if (!heyp::ReadTextProtoFromFile(inputs[i], &proto_hists[i])) {
      absl::FPrintF(stderr, "failed to read input file '%s'\n", inputs[i]);
      return 1;
    }
    hist_config.set_lowest_discernible_value(
        std::min(hist_config.lowest_discernible_value(),
                 proto_hists[i].config().lowest_discernible_value()));
    hist_config.set_highest_trackable_value(
        std::max(hist_config.highest_trackable_value(),
                 proto_hists[i].config().highest_trackable_value()));
    hist_config.set_significant_figures(
        std::max(hist_config.significant_figures(),
                 proto_hists[i].config().significant_figures()));
  }

  // Pass 2: Aggregate into a single histogram

  heyp::HdrHistogram hist(hist_config);
  for (const auto& proto_hist : proto_hists) {
    for (const auto& bkt : proto_hist.buckets()) {
      hist.RecordValues(bkt.v(), bkt.c());
    }
  }

  if (FLAGS_header) {
    absl::FPrintF(stdout, "Percentile,Value,NumSamples\n");
  }
  for (heyp::PctValue pv : hist.ToCdf()) {
    absl::FPrintF(stdout, "%f,%f,%d\n", pv.percentile, pv.value, pv.num_samples);
  }
  return 0;
}
