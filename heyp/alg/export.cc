// This file contains definitions that are exported for use in Go.

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <vector>

#include "heyp/alg/downgrade/impl-hashing.h"
#include "heyp/alg/fairness/max-min-fairness.h"
#include "ortools/algorithms/knapsack_solver.h"

namespace heyp {
namespace {

class ExportAggInfoView : public AggInfoView {
 public:
  ExportAggInfoView(uint64_t* child_ids, size_t num_children) {
    info_.reserve(num_children);
    for (size_t i = 0; i < num_children; ++i) {
      info_.push_back({.child_id = child_ids[i]});
    }
  }

  const proto::FlowInfo& parent() const override { return parent_; }
  const std::vector<ChildFlowInfo>& children() const override { return info_; }

 private:
  proto::FlowInfo parent_;
  std::vector<ChildFlowInfo> info_;
};

}  // namespace
}  // namespace heyp

extern "C" {
int64_t HeypKnapsackUsageLOPRI(int64_t* demands, size_t num_demands,
                               double want_frac_lopri, uint8_t* out_is_lopri) {
  std::optional<operations_research::KnapsackSolver> solver;

  if (num_demands <= 64) {
    solver.emplace(operations_research::KnapsackSolver::KNAPSACK_64ITEMS_SOLVER,
                   "pick-lopri");
  } else {
    solver.emplace("pick-lopri");
  }

  std::vector<int64_t> demands_vec(demands, demands + num_demands);
  int64_t total_demand = 0;
  for (int64_t demand : demands_vec) {
    total_demand += demand;
  }
  int64_t want_demand = want_frac_lopri * total_demand;
  solver->Init(demands_vec, {demands_vec}, {want_demand});
  int64_t ret = solver->Solve();
  for (size_t i = 0; i < demands_vec.size(); ++i) {
    uint8_t picked = 0;
    if (solver->BestSolutionContains(i)) {
      picked = 1;
    }
    out_is_lopri[i] = picked;
  }
  return ret;
}

int64_t HeypMaxMinFairWaterlevel(int64_t admission, int64_t* demands,
                                 size_t num_demands) {
  std::vector<int64_t> demands_vec(demands, demands + num_demands);
  heyp::SingleLinkMaxMinFairnessProblem problem;
  return problem.ComputeWaterlevel(admission, demands_vec);
}

struct HeypSelectLOPRIHashingCtx {
  heyp::HashingDowngradeSelector selector;
  heyp::ExportAggInfoView info_view;
  std::optional<spdlog::logger> logger;
};

HeypSelectLOPRIHashingCtx* NewHeypSelectLOPRIHashingCtx(uint64_t* child_ids,
                                                        size_t num_children,
                                                        int enable_logging) {
  auto p = new HeypSelectLOPRIHashingCtx{
      .info_view = heyp::ExportAggInfoView(child_ids, num_children),
  };
  if (enable_logging) {
    p->logger.emplace(heyp::MakeLogger("c-heyp-select-lopri"));
  }
  return p;
}

void FreeHeypSelectLOPRIHashingCtx(HeypSelectLOPRIHashingCtx* ctx) { delete ctx; }

void HeypSelectLOPRIHashing(HeypSelectLOPRIHashingCtx* ctx, double want_frac_lopri,
                            uint8_t* use_lopris) {
  spdlog::logger* logger = nullptr;
  if (ctx->logger.has_value()) {
    logger = &ctx->logger.value();
  }
  std::vector<bool> use_lopri_vec =
      ctx->selector.PickLOPRIChildren(ctx->info_view, want_frac_lopri, logger);
  for (size_t i = 0; i < use_lopri_vec.size(); ++i) {
    if (use_lopri_vec[i]) {
      use_lopris[i] = 1;
    } else {
      use_lopris[i] = 0;
    }
  }
}
}