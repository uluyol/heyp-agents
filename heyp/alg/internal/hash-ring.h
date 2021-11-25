#ifndef HEYP_ALG_INTERNAL_HASH_RING_H_
#define HEYP_ALG_INTERNAL_HASH_RING_H_

#include <cstdint>
#include <limits>
#include <ostream>

namespace heyp {
namespace internal {

inline constexpr uint64_t MaxId = std::numeric_limits<uint64_t>::max();

struct IdRange {
  IdRange() : lo(1), hi(0) {}
  IdRange(uint64_t from, uint64_t to) : lo(from), hi(to) {}

  uint64_t lo;  // inclusive
  uint64_t hi;  // inclusive

  bool Contains(uint64_t id) const;
};

inline bool operator==(const IdRange& lhs, const IdRange& rhs) {
  return lhs.lo == rhs.lo && lhs.hi == rhs.hi;
}

inline std::ostream& operator<<(std::ostream& os, const IdRange& r) {
  return os << "{ lo = " << r.lo << ", hi = " << r.hi << "}";
}

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

 private:
  static uint64_t FracOfRingSize(double frac);

  uint64_t start_ = 0;
  double frac_ = 0;
};

// Implementation

inline bool IdRange::Contains(uint64_t id) const { return lo <= id && id <= hi; }

inline bool RingRanges::Contains(uint64_t id) const {
  return a.Contains(id) || b.Contains(id);
}

inline void HashRing::Add(double frac_diff) {
  frac_ += frac_diff;
  if (frac_ > 1) {
    frac_ = 1;
  }
}

inline void HashRing::Sub(double frac_diff) {
  frac_ -= frac_diff;
  if (frac_ < 0) {
    frac_ = 0;
  }
  start_ += FracOfRingSize(frac_diff);
}

inline void HashRing::UpdateFrac(double frac) {
  frac_ = frac;
  if (frac_ > 1) {
    frac_ = 1;
  }
}

inline uint64_t HashRing::FracOfRingSize(double frac) {
  return frac * static_cast<double>(MaxId);
}

inline RingRanges HashRing::MatchingRanges() const {
  if (frac_ == 0) {
    return RingRanges();
  }
  uint64_t end = start_ + FracOfRingSize(frac_);
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
