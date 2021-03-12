#ifndef HEYP_STATS_CDF_H_
#define HEYP_STATS_CDF_H_

#include <cstdint>
#include <vector>

namespace heyp {

struct PctValue {
  double percentile;  // in [0, 100]
  double value;
  int64_t num_samples = 0;  // optional
};

// A Cdf contains an empiracal cumulative distribution function.
// It is a sorted vector of PctValue.
using Cdf = std::vector<PctValue>;

}  // namespace heyp

#endif  // HEYP_STATS_CDF_H_
