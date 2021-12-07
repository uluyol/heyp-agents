#include "heyp/alg/downgrade/iface.h"

#include "absl/functional/function_ref.h"
#include "absl/strings/str_cat.h"

namespace heyp {
namespace {

// Based on Go's sort.Search implementation
int Search(int n, absl::FunctionRef<bool(int)> f) {
  // Define f(-1) == false and f(n) == true.
  // Invariant: f(i-1) == false, f(j) == true.
  int i = 0;
  int j = n;
  while (i < j) {
    int h = static_cast<uint>(i + j) >> 1;  // avoid overflow when computing h
    // i ≤ h < j
    if (!f(h)) {
      i = h + 1;  // preserves f(i-1) == false
    } else {
      j = h;  // preserves f(j) == true
    }
  }
  // i == j, f(i-1) == false, and f(j) (= f(i)) == true  =>  answer is i.
  return i;
}

}  // namespace

std::vector<bool> DiffDowngradeSelectorImpl::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger) {
  DowngradeDiff diff = PickChildren(agg_info, want_frac_lopri, logger);
  const std::vector<ChildFlowInfo>& agg_children = agg_info.children();
  std::vector<bool> lopri(agg_children.size(), false);
  std::vector<std::pair<uint64_t, size_t>> id2index(agg_children.size(),
                                                    std::pair<uint64_t, size_t>{0, 0});
  for (size_t i = 0; i < agg_children.size(); ++i) {
    lopri[i] = agg_children[i].currently_lopri;
    id2index[i] = {agg_children[i].child_id, i};
  }
  std::sort(id2index.begin(), id2index.end(),
            [](const std::pair<uint64_t, size_t>& lhs,
               const std::pair<uint64_t, size_t>& rhs) { return lhs.first < rhs.first; });

  // Downgrade
  for (IdRange range : diff.to_downgrade.ranges) {
    int nexti =
        Search(id2index.size(), [&](int j) { return range.lo <= id2index[j].first; });
    while (nexti < id2index.size() && range.Contains(id2index[nexti].first)) {
      lopri[id2index[nexti].second] = true;
      ++nexti;
    }
  }
  for (uint64_t point : diff.to_downgrade.points) {
    int i = Search(id2index.size(), [&](int j) { return point <= id2index[j].first; });
    if (i < id2index.size() && id2index[i].first == point) {
      lopri[id2index[i].second] = true;
    }
  }

  // Upgrade
  for (IdRange range : diff.to_upgrade.ranges) {
    int nexti =
        Search(id2index.size(), [&](int j) { return range.lo <= id2index[j].first; });
    while (nexti < id2index.size() && range.Contains(id2index[nexti].first)) {
      lopri[id2index[nexti].second] = false;
      ++nexti;
    }
  }
  for (uint64_t point : diff.to_upgrade.points) {
    int i = Search(id2index.size(), [&](int j) { return point <= id2index[j].first; });
    if (i < id2index.size() && id2index[i].first == point) {
      lopri[id2index[i].second] = false;
    }
  }

  return lopri;
}

std::string ToString(const DowngradeDiff& diff, std::string_view indent) {
  std::string ids_indent = absl::StrCat(indent, "    ");

  return absl::StrCat(indent, "{\n", indent,
                      "  to_downgrade = ", ToString(diff.to_downgrade, ids_indent), ",\n",
                      indent, "  to_upgrade = ", ToString(diff.to_upgrade, ids_indent),
                      ",\n", indent, "}");
}

bool operator==(const DowngradeDiff& lhs, const DowngradeDiff& rhs) {
  return (lhs.to_downgrade == rhs.to_downgrade) && (lhs.to_upgrade == rhs.to_upgrade);
}

std::ostream& operator<<(std::ostream& os, const DowngradeDiff& diff) {
  return os << ToString(diff);
}

}  // namespace heyp
