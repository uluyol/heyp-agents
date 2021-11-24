#ifndef HEYP_ALG_FAIRNESS_MAX_MIN_FAIRNESS_DIST_H_
#define HEYP_ALG_FAIRNESS_MAX_MIN_FAIRNESS_DIST_H_

#include <vector>

#include "heyp/alg/fairness/max-min-fairness.h"
#include "heyp/alg/sampler.h"

namespace heyp {

// SingleLinkMaxMinFairnessProblem computes a max-min fair allocation of some
// shared capacity to the individual demands.
//
//
class SingleLinkMaxMinFairnessDistProblem {
 public:
  explicit SingleLinkMaxMinFairnessDistProblem(
      SingleLinkMaxMinFairnessProblemOptions options =
          SingleLinkMaxMinFairnessProblemOptions());

  // Computes the max-min fair waterlevel.
  double ComputeWaterlevel(double capacity, const std::vector<ValCount>& demands);

 private:
  const SingleLinkMaxMinFairnessProblemOptions options_;
  std::vector<ValCount> sorted_demands_buf_;
};

}  // namespace heyp

#endif  // HEYP_ALG_FAIRNESS_MAX_MIN_FAIRNESS_DIST_H_