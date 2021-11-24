#ifndef HEYP_ALG_FAIRNESS_MAX_MIN_FAIRNESS_H_
#define HEYP_ALG_FAIRNESS_MAX_MIN_FAIRNESS_H_

#include <stddef.h>
#include <stdint.h>

#include <cassert>
#include <vector>

#define RB_DEBUG_MAX_MIN_FAIRNESS 0

namespace heyp {

struct SingleLinkMaxMinFairnessProblemOptions {
  enum SolveMethod {
    kFullSort,
    kPartialSort,
  };

  // Runtime complexity is O(N * log(N)) if set to kFullSort
  // or O(N) if set to kPartialSort [where N = demands.size()].
  SolveMethod solve_method = kPartialSort;

  // Enables fast path for handling tiny flows.
  bool enable_tiny_flow_opt = true;
};

std::ostream& operator<<(std::ostream& os,
                         const SingleLinkMaxMinFairnessProblemOptions& options);

// SingleLinkMaxMinFairnessProblem computes a max-min fair allocation of some
// shared capacity to the individual demands.
//
//
class SingleLinkMaxMinFairnessProblem {
 public:
  explicit SingleLinkMaxMinFairnessProblem(
      SingleLinkMaxMinFairnessProblemOptions options =
          SingleLinkMaxMinFairnessProblemOptions());

  // Computes the max-min fair waterlevel.
  int64_t ComputeWaterlevel(int64_t capacity,
                            const std::vector<int64_t>& demands);

  // Sets allocations[i][j] = min(demands[i][j], waterlevel).
  void SetAllocations(int64_t waterlevel,
                      const std::vector<int64_t>& demands,
                      std::vector<int64_t>* allocations);

 private:
  const SingleLinkMaxMinFairnessProblemOptions options_;
  std::vector<int64_t> sorted_demands_buf_;
};

}  // namespace heyp

#endif  // HEYP_ALG_FAIRNESS_MAX_MIN_FAIRNESS_H_