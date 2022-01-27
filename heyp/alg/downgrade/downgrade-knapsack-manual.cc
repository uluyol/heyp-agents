#include <cstdint>
#include <iostream>
#include <vector>

#include "absl/strings/str_join.h"
#include "heyp/alg/agg-info-views.h"
#include "heyp/alg/downgrade/formatters.h"
#include "heyp/alg/downgrade/impl-knapsack-solver.h"
#include "heyp/init/init.h"
#include "heyp/proto/heyp.pb.h"
#include "third_party/xxhash/xxhash.h"

namespace heyp {

static void PutLittleEndian(uint64_t v, char* b) {
  b[0] = v;
  b[1] = v >> 8;
  b[2] = v >> 16;
  b[3] = v >> 24;
  b[4] = v >> 32;
  b[5] = v >> 40;
  b[6] = v >> 48;
  b[7] = v >> 56;
}

static void Run(double want_frac_lopri, const std::vector<int64_t>& usages) {
  proto::AggInfo info;
  char data[8];
  for (int i = 0; i < usages.size(); ++i) {
    PutLittleEndian(i, data);
    uint64_t child_id = XXH64(data, 8, 0);
    proto::FlowInfo* child = info.add_children();
    child->mutable_flow()->set_host_id(child_id);
    child->set_ewma_usage_bps(usages[i]);
  }

  HostLevelView view = HostLevelView::Create<FVSource::kUsage>(info);
  KnapsackSolverDowngradeSelector selector;
  spdlog::logger logger = MakeLogger("main");
  std::vector<bool> lopri = selector.PickLOPRIChildren(view, want_frac_lopri, &logger);
  std::cout << absl::StrJoin(lopri, "", BitmapFormatter()) << "\n";
}

void PrintUsageAndExit(char** argv) {
  std::cerr << absl::StrCat("usage: ", argv[0], " lopri_frac usage...\n");
  exit(2);
}

}  // namespace heyp

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);
  if (argc < 2) {
    heyp::PrintUsageAndExit(argv);
  }

  double want_frac_lopri = 0;
  if (!absl::SimpleAtod(argv[1], &want_frac_lopri)) {
    heyp::PrintUsageAndExit(argv);
  }
  std::vector<int64_t> usages;
  for (int i = 2; i < argc; ++i) {
    int64_t v = 0;
    if (!absl::SimpleAtoi(argv[i], &v)) {
      heyp::PrintUsageAndExit(argv);
    }
    usages.push_back(v);
  }
  heyp::Run(want_frac_lopri, usages);
  return 0;
}
