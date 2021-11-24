#include "heyp/alg/fairness/max-min-fairness-dist.h"

#include <cstdint>
#include <iostream>

#include "absl/algorithm/container.h"
#include "absl/base/macros.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "heyp/alg/fairness/nth-element.h"

namespace heyp {

namespace {

constexpr bool kDebugAllocator = false;

struct ValCountComparator {
  bool operator()(const ValCount& lhs, const ValCount& rhs) { return lhs.val < rhs.val; }
};

// Simpler, slower allocation method. Used for testing / comparison.
double SolveFullSort(double capacity, double waterlevel,
                     const std::vector<ValCount>& demands,
                     absl::Span<ValCount> sorted_demands) {
  absl::c_sort(sorted_demands, ValCountComparator());

  std::vector<double> expected_ge_count(sorted_demands.size(), 0);
  {
    double cum_count = 0;
    for (int i = sorted_demands.size() - 1; i >= 0; i--) {
      cum_count += sorted_demands[i].expected_count;
      expected_ge_count[i] = cum_count;
    }
  }

  // Progressively compute the waterlevel, and mark any demands that can be
  // satisfied as we go.

  size_t next = 0;
  while (next < sorted_demands.size()) {
    ValCount& next_demand = sorted_demands[next];

    const double delta = next_demand.val - waterlevel;
    const double num_unsatisfied = expected_ge_count[next];

    const double ask = delta * num_unsatisfied;
    if (ask <= capacity) {
      waterlevel += delta;
      capacity -= ask;
      next++;
    } else {
      // Since we cannot satisfy any more demands, evenly divide the remaining
      // capacity across the unsatisfied demands.
      waterlevel += capacity / num_unsatisfied;
      break;
    }
  }

  return waterlevel;
}

double SumCounts(absl::Span<ValCount> data) {
  double sum = 0;
  for (ValCount x : data) {
    sum += x.expected_count;
  }
  return sum;
}

class PartialSortAllocator {
 public:
  PartialSortAllocator(double original_capacity, double capacity, double waterlevel,
                       const std::vector<ValCount>& demands,
                       absl::Span<ValCount> sorted_demands)
      : capacity_(original_capacity),
        demands_(demands),
        sorted_demands_(sorted_demands),
        residual_capacity_(capacity),
        lower_limit_(0),
        upper_limit_(static_cast<ssize_t>(sorted_demands.size()) - 1),
        count_above_upper_limit_(0),
        waterlevel_(waterlevel) {}

  PartialSortAllocator(const PartialSortAllocator&) = delete;
  PartialSortAllocator& operator=(const PartialSortAllocator&) = delete;

  double ComputeWaterlevel();

 private:
  void PrintState();
  bool PrintInvariantViolations();

  const double capacity_;
  const std::vector<ValCount>& demands_;
  const absl::Span<ValCount> sorted_demands_;

  // Current area of interest is [lower_limit, upper_limit].
  //
  // Invariants:
  // 1. For any i > upper_limit: demand(i) cannot be satisfied and will get the
  //    final waterlevel.
  // 2. For any i < lower_limit:
  //    a. demand(i) *is* satisfied
  //    b. waterlevel ≥ demand(i)
  // 3. residual_capacity = capacity
  //                        - ∑[i<lower_limit] demand(i)
  //                        - waterlevel *
  //                        ∑(sorted_demands_[lower_limit:].expected_count)
  // 4. count_above_upper_limit_ = ∑(sorted_demands_[upper_limit+1:].expected_count)
  double residual_capacity_;
  ssize_t lower_limit_;  // inclusive
  ssize_t upper_limit_;  // inclusive
  double count_above_upper_limit_;
  double waterlevel_;
};

bool PartialSortAllocator::PrintInvariantViolations() {
  bool found_violation = false;

  double sum_count_above_upper_limit = 0;
  for (size_t i = upper_limit_ + 1; i < sorted_demands_.size(); i++) {
    sum_count_above_upper_limit += sorted_demands_[i].expected_count;
  }
  if (std::abs(sum_count_above_upper_limit - count_above_upper_limit_) > 0.001) {
    found_violation = true;
    std::cerr << absl::Substitute("count_above_upper_limits != expected: $0 vs $1",
                                  count_above_upper_limit_, sum_count_above_upper_limit);
  }

  for (size_t i = lower_limit_; i < sorted_demands_.size(); i++) {
    double demand = sorted_demands_[i].val;
    if (demand < waterlevel_) {
      found_violation = true;
      std::cerr << absl::Substitute(
          "demands < waterlevel: demand = $0 (sorted_demands[$1]) waterlevel = "
          "$2\n",
          demand, i, waterlevel_);
    }
  }

  double sum_lower_demands = 0;
  for (size_t i = 0; i < lower_limit_; i++) {
    double demand = sorted_demands_[i].val;
    sum_lower_demands += demand * sorted_demands_[i].expected_count;
    if (demand > waterlevel_) {
      found_violation = true;
      std::cerr << absl::Substitute(
          "demands > waterlevel: demand = $0 (sorted_demands[$1]) waterlevel = "
          "$2\n",
          demand, i, waterlevel_);
    }
  }

  double expected_residual_capacity =
      capacity_ - sum_lower_demands -
      waterlevel_ *
          SumCounts(sorted_demands_.subspan(lower_limit_, sorted_demands_.npos));

  if (residual_capacity_ != expected_residual_capacity) {
    found_violation = true;
    std::cerr << absl::Substitute(
        "residual_capacity != expected: residual_capacity = $0 expected = $1\n",
        residual_capacity_, expected_residual_capacity);
  }

  if (found_violation) {
    PrintState();
  }

  return found_violation;
}

void PartialSortAllocator::PrintState() {
  std::cerr << "waterlevel: " << waterlevel_ << "\n";
  std::cerr << "demands: [" << absl::StrJoin(demands_, " ", absl::StreamFormatter())
            << "]\n";
  std::cerr << "lower_limit: " << lower_limit_ << " upper_limit: " << upper_limit_
            << "\n";
  std::cerr << "sorted_demands: ["
            << absl::StrJoin(sorted_demands_, " ", absl::StreamFormatter()) << "]\n";
}

double PartialSortAllocator::ComputeWaterlevel() {
  if (kDebugAllocator) {
    PrintState();
  }

  // This algorithm is based on partial sorting.
  //
  // The high-level level idea is to quickly identify which demands are
  // satisfiable and which are not without fully sorting all demands.
  //
  // If we have demands partitioned into A=[0, m] and B=[m+1, n] such that
  // max(A) ≤ max(B) and ∑A + max(A) * (demands.size()-m) ≤ capacity, then we
  // know that all of A can be satisfied and we don't need to bother sorting
  // it.
  //
  // Similarly, if we know that max(A) ≤ max(B) and ∑A + max(A) > capacity,
  // then we don't need to bother sorting B since the demands in A are smaller
  // and we are unable to satisfy them.

  if (sorted_demands_.empty()) {
    return waterlevel_;
  }

  while (upper_limit_ >= lower_limit_) {
    ABSL_ASSERT(!PrintInvariantViolations());
    // Pick a partition point so that
    //     A=[lower_limit, partition_idx]
    //     B=[partition_idx+1, upper_limit]
    size_t partition_idx = lower_limit_ + (upper_limit_ - lower_limit_) / 2;
    NthElement(sorted_demands_.begin() + lower_limit_,
               sorted_demands_.begin() + partition_idx,
               sorted_demands_.begin() + upper_limit_ + 1, ValCountComparator());

    // Compute ask = ∑A + max(A) * |B|
    double max_demand_A = 0;
    double ask = 0;
    for (size_t i = lower_limit_; i <= partition_idx; i++) {
      ValCount vc = sorted_demands_[i];
      ask += vc.val * vc.expected_count - waterlevel_;
      max_demand_A = vc.val;  // sorted_demands_[partition_idx] has greatest demand
    }
    double expected_count_B =
        SumCounts(sorted_demands_.subspan(partition_idx + 1, upper_limit_));
    ask += (max_demand_A - waterlevel_) * (expected_count_B + count_above_upper_limit_);

    if (kDebugAllocator) {
      std::cerr << "---\n";
      std::cerr << "max_demand_A: " << max_demand_A << " ask: " << ask
                << " partition: " << partition_idx << "\n"
                << "residual_capacity: " << residual_capacity_ << "\n";
      PrintState();
      std::cerr << "---\n";
    }

    // Check if we can allocate A's demands
    if (ask <= residual_capacity_) {
      // Adjust waterlevel_ and residual_capacity_.
      // Continue the search in B.
      waterlevel_ = max_demand_A;
      residual_capacity_ -= ask;
      lower_limit_ = partition_idx + 1;
    } else if (lower_limit_ == upper_limit_) {
      count_above_upper_limit_ +=
          SumCounts(sorted_demands_.subspan(lower_limit_, upper_limit_));
      upper_limit_ = lower_limit_ - 1;
    } else {
      // Cannot allocate A. Don't need to even try B.
      // Continue the search in A.
      count_above_upper_limit_ += expected_count_B;
      upper_limit_ = partition_idx;
    }
  }
  ABSL_ASSERT(!PrintInvariantViolations());

  // Since we cannot satisfy any more demands, evenly divide the remaining
  // capacity across the unsatisfied demands.
  ssize_t next_unsatisfied = std::max(lower_limit_, upper_limit_);
  if (next_unsatisfied < sorted_demands_.size()) {
    if (kDebugAllocator) {
      std::cerr << "waterlevel before dividing remainder: " << waterlevel_ << "\n";
    }
    waterlevel_ += residual_capacity_ / SumCounts(sorted_demands_.subspan(
                                            next_unsatisfied, sorted_demands_.npos));
  }

  if (kDebugAllocator) {
    std::cerr << "final waterlevel: " << waterlevel_ << "\n";
  }

  return waterlevel_;
}

// Faster allocation method.
double SolvePartialSort(double original_capacity, double capacity, double waterlevel,
                        const std::vector<ValCount>& demands,
                        absl::Span<ValCount> sorted_demands) {
  PartialSortAllocator allocator(original_capacity, capacity, waterlevel, demands,
                                 sorted_demands);
  return allocator.ComputeWaterlevel();
}

}  // namespace

SingleLinkMaxMinFairnessDistProblem::SingleLinkMaxMinFairnessDistProblem(
    SingleLinkMaxMinFairnessProblemOptions options)
    : options_(std::move(options)) {}

double SingleLinkMaxMinFairnessDistProblem::ComputeWaterlevel(
    double capacity, const std::vector<ValCount>& demands) {
  ABSL_ASSERT(capacity >= 0);

  int num_demands = demands.size();

  // Sort all demands in increasing order to make it easy to track how many
  // demands have been satisfied (or not).
  //
  // Also, filter out any demands that are smaller than capacity / num_demands
  // as they are guaranteed to be satisfied.

  double tiny_demand_thresh = capacity / std::max<double>(num_demands, 1);
  if (!options_.enable_tiny_flow_opt) {
    tiny_demand_thresh = -1;
  }

  sorted_demands_buf_.resize(num_demands, ValCount{});
  size_t num_unfiltered = 0;
  double waterlevel = 0;
  for (uint32_t i = 0; i < demands.size(); i++) {
    sorted_demands_buf_[num_unfiltered] = demands[i];
    if (demands[i].val <= tiny_demand_thresh) {
      capacity -= demands[i].val * demands[i].expected_count;
      waterlevel = std::max<double>(waterlevel, demands[i].val);
    } else {
      num_unfiltered++;
    }
  }
  const double capacity_without_tiny = capacity;
  capacity -= waterlevel * num_unfiltered;

  switch (options_.solve_method) {
    case SingleLinkMaxMinFairnessProblemOptions::kFullSort:
      waterlevel =
          SolveFullSort(capacity, waterlevel, demands,
                        absl::MakeSpan(sorted_demands_buf_).subspan(0, num_unfiltered));
      break;
    case SingleLinkMaxMinFairnessProblemOptions::kPartialSort:
      waterlevel = SolvePartialSort(
          capacity_without_tiny, capacity, waterlevel, demands,
          absl::MakeSpan(sorted_demands_buf_).subspan(0, num_unfiltered));
      break;
  }

  return waterlevel;
}

}  // namespace heyp
