#ifndef HEYP_ALG_INTERNAL_HASH_RING_H_
#define HEYP_ALG_INTERNAL_HASH_RING_H_

#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string>

#include "heyp/alg/unordered-ids.h"

namespace heyp {
namespace internal {

inline constexpr uint64_t MaxId = std::numeric_limits<uint64_t>::max();

struct RingRanges {
  IdRange a;
  IdRange b;

  bool Contains(uint64_t id) const;
};

inline bool operator==(const RingRanges& lhs, const RingRanges& rhs) {
  return lhs.a == rhs.a && lhs.b == rhs.b;
}

class HashRing {
 public:
  void Add(double frac_diff);
  void Sub(double frac_diff);
  void UpdateFrac(double frac);

  RingRanges MatchingRanges() const;
  std::string ToString() const;

  // Exposed for testing
  static uint64_t FracToRing(double frac);

 private:
  uint64_t start_ = 0;
  double frac_ = 0;
};

// Implementation

inline bool RingRanges::Contains(uint64_t id) const {
  return a.Contains(id) || b.Contains(id);
}

inline void HashRing::Add(double frac_diff) { UpdateFrac(frac_ + frac_diff); }

inline void HashRing::Sub(double frac_diff) { UpdateFrac(frac_ - frac_diff); }

inline void HashRing::UpdateFrac(double frac) {
  if (frac < 0) {
    frac = 0;
  } else if (frac > 1) {
    frac = 1;
  }
  if (frac_ > frac) {
    start_ += FracToRing(frac_ - frac);
  }
  frac_ = frac;
  if (frac_ > 1) {
    frac_ = 1;
  }
}

inline uint64_t HashRing::FracToRing(double frac) {
  // Naively we want to do frac * MaxId, but this doesn't do well with rounding.
  // In particular, 1.0 * MaxId != MaxId which is unfortunate.
  //
  // To work around this, divide the ring into 2^32 chunks and count how many chunks frac
  // corresponds to. This ensures that FracToRing(1.0) == MaxId.
  static constexpr uint64_t kNumChunks = static_cast<uint64_t>(1) << 32;
  static constexpr uint64_t kChunkSize = kNumChunks;
  uint64_t matched_chunks = round(frac * kNumChunks);

  if (matched_chunks == 0) {
    return 0;
  }

  // Inclusive, subtract one.
  return matched_chunks * kChunkSize - 1;
}

inline RingRanges HashRing::MatchingRanges() const {
  if (frac_ == 0) {
    return RingRanges();
  }
  uint64_t end = start_ + FracToRing(frac_);
  if (end < start_) {
    return RingRanges{
        IdRange{0, end},
        IdRange{start_, MaxId},
    };
  }
  return RingRanges{
      IdRange{start_, end},  // actual range
      IdRange{},             // matches nothing
  };
}

}  // namespace internal
}  // namespace heyp

#endif  // HEYP_ALG_INTERNAL_HASH_RING_H_
