#ifndef HEYP_ALG_UNORDERED_IDS_H_
#define HEYP_ALG_UNORDERED_IDS_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace heyp {

struct IdRange {
  IdRange() : lo(1), hi(0) {}
  IdRange(uint64_t from, uint64_t to) : lo(from), hi(to) {}
  IdRange(const IdRange& other) : lo(other.lo), hi(other.hi) {}

  uint64_t lo;  // inclusive
  uint64_t hi;  // inclusive

  bool Contains(uint64_t id) const;
  bool Empty() const;
};

struct UnorderedIds {
  std::vector<IdRange> ranges;
  std::vector<uint64_t> points;
};

inline bool operator==(const IdRange& lhs, const IdRange& rhs) {
  return lhs.lo == rhs.lo && lhs.hi == rhs.hi;
}

bool operator==(const UnorderedIds& lhs, const UnorderedIds& rhs);

std::string ToString(IdRange interval);
std::string ToString(const UnorderedIds& set, std::string_view indent = "");

std::ostream& operator<<(std::ostream& os, IdRange interval);
std::ostream& operator<<(std::ostream& os, const UnorderedIds& set);

inline bool IdRange::Contains(uint64_t id) const { return lo <= id && id <= hi; }

inline bool IdRange::Empty() const { return hi < lo; }

}  // namespace heyp

#endif  // HEYP_ALG_UNORDERED_IDS_H_
