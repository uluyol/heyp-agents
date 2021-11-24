#include "heyp/alg/fairness/max-min-fairness.h"

#include <iostream>

#include "absl/algorithm/container.h"
#include "absl/base/macros.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "heyp/alg/fairness/nth-element.h"

namespace heyp {

namespace {

constexpr bool kDebugAllocator = false;

// Simpler, slower allocation method. Used for testing / comparison.
int64_t SolveFullSort(int64_t capacity, int64_t waterlevel,
                      const std::vector<int64_t>& demands,
                      absl::Span<int64_t> sorted_demands) {
  absl::c_sort(sorted_demands);

  // Progressively compute the waterlevel, and mark any demands that can be
  // satisfied as we go.

  size_t next = 0;
  while (next < sorted_demands.size()) {
    int64_t& next_demand = sorted_demands[next];

    const int64_t delta = next_demand - waterlevel;
    const int64_t num_unsatisfied = sorted_demands.size() - next;

    const int64_t ask = delta * num_unsatisfied;
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

class PartialSortAllocator {
 public:
  PartialSortAllocator(int64_t original_capacity, int64_t capacity, int64_t waterlevel,
                       const std::vector<int64_t>& demands,
                       absl::Span<int64_t> sorted_demands)
      : capacity_(original_capacity),
        demands_(demands),
        sorted_demands_(sorted_demands),
        residual_capacity_(capacity),
        lower_limit_(0),
        upper_limit_(static_cast<ssize_t>(sorted_demands.size()) - 1),
        waterlevel_(waterlevel) {}

  PartialSortAllocator(const PartialSortAllocator&) = delete;
  PartialSortAllocator& operator=(const PartialSortAllocator&) = delete;

  int64_t ComputeWaterlevel();

 private:
  void PrintState();
  bool PrintInvariantViolations();

  const int64_t capacity_;
  const std::vector<int64_t>& demands_;
  const absl::Span<int64_t> sorted_demands_;

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
  //                        - waterlevel * (sorted_demands_.size() -
  //                        lower_limit)
  int64_t residual_capacity_;
  ssize_t lower_limit_;  // inclusive
  ssize_t upper_limit_;  // inclusive
  int64_t waterlevel_;
};

struct BpsVecFormatter {
  void operator()(std::string* out, const std::vector<int64_t>& vs) const {
    out->append("[");
    out->append(absl::StrJoin(vs, " ", absl::AlphaNumFormatter()));
    out->append("]");
  }
};

bool PartialSortAllocator::PrintInvariantViolations() {
  bool found_violation = false;

  for (size_t i = lower_limit_; i < sorted_demands_.size(); i++) {
    int64_t demand = sorted_demands_[i];
    if (demand < waterlevel_) {
      found_violation = true;
      std::cerr << absl::Substitute(
          "demands < waterlevel: demand = $0 (sorted_demands[$1]) waterlevel = "
          "$2\n",
          demand, i, waterlevel_);
    }
  }

  int64_t sum_lower_demands = 0;
  for (size_t i = 0; i < lower_limit_; i++) {
    int64_t demand = sorted_demands_[i];
    sum_lower_demands += demand;
    if (demand > waterlevel_) {
      found_violation = true;
      std::cerr << absl::Substitute(
          "demands > waterlevel: demand = $0 (sorted_demands[$1]) waterlevel = "
          "$2\n",
          demand, i, waterlevel_);
    }
  }

  int64_t expected_residual_capacity =
      capacity_ - sum_lower_demands -
      waterlevel_ * (sorted_demands_.size() - lower_limit_);

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
  std::cerr << "demands: [" << absl::StrJoin(demands_, " ", absl::AlphaNumFormatter())
            << "]\n";
  std::cerr << "lower_limit: " << lower_limit_ << " upper_limit: " << upper_limit_
            << "\n";
  std::cerr << "sorted_index: ["
            << absl::StrJoin(sorted_demands_, " ", absl::AlphaNumFormatter()) << "]\n";
  std::vector<int64_t> sorted_demands(sorted_demands_.size(), 0);
  for (size_t i = 0; i < sorted_demands_.size(); i++) {
    sorted_demands[i] = sorted_demands_[i];
  }
  std::cerr << "sorted_demands: ["
            << absl::StrJoin(sorted_demands, " ", absl::AlphaNumFormatter()) << "]\n";
}

int64_t PartialSortAllocator::ComputeWaterlevel() {
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
               sorted_demands_.begin() + upper_limit_ + 1);

    // Compute ask = ∑A + max(A) * |B|
    int64_t max_demand_A = 0;
    int64_t ask = 0;
    for (size_t i = lower_limit_; i <= partition_idx; i++) {
      int64_t d = sorted_demands_[i];
      ask += d - waterlevel_;
      max_demand_A = d;  // sorted_demands_[partition_idx] has greatest demand
    }
    ask += (max_demand_A - waterlevel_) * (sorted_demands_.size() - partition_idx - 1);

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
      upper_limit_ = lower_limit_ - 1;
    } else {
      // Cannot allocate A. Don't need to even try B.
      // Continue the search in A.
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
    waterlevel_ += residual_capacity_ / (sorted_demands_.size() - next_unsatisfied);
  }

  if (kDebugAllocator) {
    std::cerr << "final waterlevel: " << waterlevel_ << "\n";
  }

  return waterlevel_;
}

// Faster allocation method.
int64_t SolvePartialSort(int64_t original_capacity, int64_t capacity, int64_t waterlevel,
                         const std::vector<int64_t>& demands,
                         absl::Span<int64_t> sorted_demands) {
  PartialSortAllocator allocator(original_capacity, capacity, waterlevel, demands,
                                 sorted_demands);
  return allocator.ComputeWaterlevel();
}

}  // namespace

SingleLinkMaxMinFairnessProblem::SingleLinkMaxMinFairnessProblem(
    SingleLinkMaxMinFairnessProblemOptions options)
    : options_(std::move(options)) {}

int64_t SingleLinkMaxMinFairnessProblem::ComputeWaterlevel(
    int64_t capacity, const std::vector<int64_t>& demands) {
  ABSL_ASSERT(capacity >= 0);

  int64_t num_demands = demands.size();

  // Sort all demands in increasing order to make it easy to track how many
  // demands have been satisfied (or not).
  //
  // Also, filter out any demands that are smaller than capacity / num_demands
  // as they are guaranteed to be satisfied.

  int64_t tiny_demand_thresh = capacity / std::max(num_demands, static_cast<int64_t>(1));
  if (!options_.enable_tiny_flow_opt) {
    tiny_demand_thresh = -1;
  }

  sorted_demands_buf_.resize(num_demands, 0);
  size_t num_unfiltered = 0;
  int64_t waterlevel = 0;
  for (uint32_t i = 0; i < demands.size(); i++) {
    sorted_demands_buf_[num_unfiltered] = demands[i];
    if (demands[i] <= tiny_demand_thresh) {
      capacity -= demands[i];
      waterlevel = std::max(waterlevel, demands[i]);
    } else {
      num_unfiltered++;
    }
  }
  const int64_t capacity_without_tiny = capacity;
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

void SingleLinkMaxMinFairnessProblem::SetAllocations(int64_t waterlevel,
                                                     const std::vector<int64_t>& demands,
                                                     std::vector<int64_t>* allocations) {
  allocations->resize(demands.size(), 0);
  for (size_t i = 0; i < demands.size(); i++) {
    (*allocations)[i] = std::min(waterlevel, demands[i]);
  }
}

std::ostream& operator<<(std::ostream& os,
                         const SingleLinkMaxMinFairnessProblemOptions& options) {
  std::string solve_method = "unknown";
  switch (options.solve_method) {
    case SingleLinkMaxMinFairnessProblemOptions::kFullSort:
      solve_method = "kFullSort";
      break;
    case SingleLinkMaxMinFairnessProblemOptions::kPartialSort:
      solve_method = "kPartialSort";
      break;
  }
  return os << absl::Substitute("{solve_method: $0, enable_tiny_flow_opt; $1}",
                                solve_method, options.enable_tiny_flow_opt);
}  // namespace rb

}  // namespace heyp
