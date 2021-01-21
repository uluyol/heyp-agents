#ifndef HEYP_ALG_RATE_LIMITS_H_
#define HEYP_ALG_RATE_LIMITS_H_

#include <cstdint>
#include <ostream>

#include "heyp/proto/heyp.pb.h"

namespace heyp {

struct RateLimits {
  int64_t hipri_limit_bps;
  int64_t lopri_limit_bps;
};

std::ostream& operator<<(std::ostream& os, const RateLimits& limits);

// BweBurstinessFactor computes the 'burstiness' of the cluster-fg as described
// in Section 6.2 of the BwE paper.
//
// The value is equal to ( ∑ host demands / cluster demand) and is higher the
// more usage across hosts is uncorrelated.
//
// The main use-case for this value is for allocating rate limits:
//
// - Assume that the Global Broker gives a cluster-fg a rate limit of L.
//
// - If we directly compute host-level rate limits that sum to L, then hosts
//   will not be allowed to burst higher than their rate limits, even if hosts
//   burst at separate times.
//
// - On the other hand, if we first multiple L by the burstiness factor and then
//   compute host-level rate limits, we will accomodate the bursts while still
//   obeying L on aggregate - assuming that the burstiness of demand remains
//   constant over time.
//
double BweBurstinessFactor(const proto::AggInfo& info);

// EvenlyDistributeExtra computes how must extra bandwidth can be given to each
// child if evenly distributed.
int64_t EvenlyDistributeExtra(int64_t admission,
                              const std::vector<int64_t>& demands,
                              int64_t waterlevel);

}  // namespace heyp

#endif  // HEYP_ALG_RATE_LIMITS_H_