#ifndef HEYP_ALG_UNORDERED_IDS_H_
#define HEYP_ALG_UNORDERED_IDS_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace heyp {

struct IdRange {
  IdRange() : lo(1), hi(0) {}
  IdRange(uint64_t from, uint64_t to) : lo(from), hi(to) {}

  uint64_t lo;  // inclusive
  uint64_t hi;  // inclusive

  bool Contains(uint64_t id) const;
};

struct UnorderedIds {
  std::vector<IdRange> ranges;
  std::vector<uint64_t> points;
};

inline bool operator==(const IdRange& lhs, const IdRange& rhs) {
  return lhs.lo == rhs.lo && lhs.hi == rhs.hi;
}

inline bool IdRange::Contains(uint64_t id) const { return lo <= id && id <= hi; }

std::string ToString(IdRange interval);
std::string ToString(const UnorderedIds& set);

std::ostream& operator<<(std::ostream& os, IdRange interval);
std::ostream& operator<<(std::ostream& os, const UnorderedIds& set);

}  // namespace heyp

#endif  // HEYP_ALG_UNORDERED_IDS_H_
