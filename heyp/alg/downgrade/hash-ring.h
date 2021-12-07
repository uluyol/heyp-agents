#ifndef HEYP_ALG_DOWNGRADE_HASH_RING_H_
#define HEYP_ALG_DOWNGRADE_HASH_RING_H_

#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string>

#include "absl/base/macros.h"
#include "absl/numeric/int128.h"
#include "heyp/alg/unordered-ids.h"

namespace heyp {

inline constexpr uint64_t MaxId = std::numeric_limits<uint64_t>::max();

struct RingRanges {
  IdRange a;
  IdRange b;

  bool Contains(uint64_t id) const;
};

inline bool operator==(const RingRanges& lhs, const RingRanges& rhs) {
  return lhs.a == rhs.a && lhs.b == rhs.b;
}

std::string ToString(const RingRanges& r);
std::ostream& operator<<(std::ostream& os, const RingRanges& r);

enum class RangeDiffType { kAdd, kDel };

struct RangeDiff {
  RingRanges diff;
  RangeDiffType type = RangeDiffType::kAdd;
};

inline bool operator==(const RangeDiff& lhs, const RangeDiff& rhs) {
  return (lhs.diff == rhs.diff) && (lhs.type == rhs.type);
}

std::string ToString(RangeDiffType t);
std::string ToString(const RangeDiff& d);
std::ostream& operator<<(std::ostream& os, const RangeDiff& d);

class HashRing {
 public:
  RangeDiff Add(double frac_diff);
  RangeDiff Sub(double frac_diff);
  RangeDiff UpdateFrac(double frac);

  RingRanges MatchingRanges() const;
  std::string ToString() const;

  // Exposed for testing
  static absl::uint128 FracToRing(double frac);
  static RangeDiff ComputeRangeDiff(uint64_t old_start, double old_frac,
                                    uint64_t new_start, double new_frac);

  static constexpr uint64_t kNumChunks = static_cast<uint64_t>(1) << 32;
  static constexpr uint64_t kChunkSize = kNumChunks;

 private:
  uint64_t start_ = 0;
  double frac_ = 0;
};

// Implementation

inline bool RingRanges::Contains(uint64_t id) const {
  return a.Contains(id) || b.Contains(id);
}

inline RangeDiff HashRing::Add(double frac_diff) { return UpdateFrac(frac_ + frac_diff); }

inline RangeDiff HashRing::Sub(double frac_diff) { return UpdateFrac(frac_ - frac_diff); }

inline RangeDiff HashRing::UpdateFrac(double frac) {
  if (frac < 0) {
    frac = 0;
  } else if (frac > 1) {
    frac = 1;
  }

  const uint64_t old_start = start_;
  const double old_frac = frac_;

  if (frac_ > frac) {
    start_ += absl::Uint128Low64(FracToRing(frac_ - frac));
  }
  frac_ = frac;

  return ComputeRangeDiff(old_start, old_frac, start_, frac_);
}

inline absl::uint128 HashRing::FracToRing(double frac) {
  // Naively we want to do frac * MaxId, but this doesn't do well with rounding.
  // In particular, 1.0 * MaxId != MaxId which is unfortunate.
  //
  // To work around this, divide the ring into 2^32 chunks and count how many chunks frac
  // corresponds to. This ensures that FracToRing(1.0) == MaxId.
  uint64_t matched_chunks = round(frac * kNumChunks);

  if (matched_chunks == 0) {
    return 0;
  }

  // Inclusive, subtract one.
  return absl::uint128(matched_chunks) * kChunkSize;
}

inline RangeDiff HashRing::ComputeRangeDiff(uint64_t old_start, double old_frac,
                                            uint64_t new_start, double new_frac) {
  if (old_frac == new_frac) {
    ABSL_ASSERT(old_start == new_start);
    return RangeDiff();
  }

  absl::uint128 old_end = old_start;
  old_end += FracToRing(old_frac) - 1;
  absl::uint128 new_end = new_start;
  new_end += FracToRing(new_frac) - 1;

  RangeDiff diff;
  if (old_frac < new_frac) {
    ABSL_ASSERT(old_start == new_start);
    uint64_t new_end_lo = absl::Uint128Low64(new_end);
    absl::uint128 old_end_p1 = old_end + 1;
    uint64_t old_end_p1_lo = absl::Uint128Low64(old_end_p1);

    uint64_t new_end_hi = absl::Uint128High64(new_end);
    uint64_t old_end_p1_hi = absl::Uint128High64(old_end_p1);
    if (new_end_hi != 0 && old_end_p1_hi == 0) {
      // old_end_p1 does not wrap around.
      return RangeDiff{.diff = RingRanges{.a = IdRange(0, new_end_lo),
                                          .b = IdRange(old_end_p1_lo, MaxId)},
                       .type = RangeDiffType::kAdd};
    }
    // Either both wrap around, or none. In either case, use the diff.
    return RangeDiff{.diff = RingRanges{.a = IdRange(old_end_p1_lo, new_end_lo)},
                     .type = RangeDiffType::kAdd};
  }

  // Shrink marked space
  ABSL_ASSERT(old_end == new_end);
  if (old_start < new_start) {
    // new_start-1 cannot underflow since new_start > old_start
    return RangeDiff{.diff = RingRanges{IdRange(old_start, new_start - 1)},
                     .type = RangeDiffType::kDel};
  } else if (new_start == 0) {
    return RangeDiff{.diff = RingRanges{IdRange(old_start, MaxId)},
                     .type = RangeDiffType::kDel};
  }
  // new_start-1 cannot underflow since new_start > 0
  return RangeDiff{
      .diff = RingRanges{IdRange(0, new_start - 1), IdRange(old_start, MaxId)},
      .type = RangeDiffType::kDel};
}

inline RingRanges HashRing::MatchingRanges() const {
  if (frac_ == 0) {
    return RingRanges();
  }
  absl::uint128 end = start_;
  end += FracToRing(frac_);
  end -= 1;
  uint64_t end_low = absl::Uint128Low64(end);
  if (absl::Uint128High64(end) != 0) {
    return RingRanges{IdRange(0, end_low), IdRange(start_, MaxId)};
  }
  return RingRanges{
      IdRange(start_, end_low),  // actual range
      IdRange{},                 // matches nothing
  };
}

}  // namespace heyp

#endif  // HEYP_ALG_DOWNGRADE_HASH_RING_H_
