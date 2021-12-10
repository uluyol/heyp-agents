#include "heyp/alg/downgrade/iface.h"

#include "absl/functional/function_ref.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/downgrade/formatters.h"

namespace heyp {

std::vector<bool> DiffDowngradeSelectorImpl::PickLOPRIChildren(
    const AggInfoView& agg_info, const double want_frac_lopri, spdlog::logger* logger) {
  DowngradeDiff diff = PickChildren(agg_info, want_frac_lopri, logger);
  // Convert diff to a linear mask in O(#children) time and space.
  //
  // For each range, search the children (using a linear scan) and mark matches.
  // Typically, we should only have a few ranges (e.g. hashing returns ≤ 4),
  // so that is effectively a constant factor.
  //
  // For point matches, populate and lookup using a hash table.

  const std::vector<ChildFlowInfo>& agg_children = agg_info.children();
  std::vector<bool> lopri(agg_children.size(), false);
  std::optional<absl::flat_hash_map<uint64_t, size_t>> id2index;

  auto lazy_init_id2index = [&] {
    if (id2index) {
      // already initialized
      return;
    }
    id2index.emplace();
    for (size_t i = 0; i < agg_children.size(); ++i) {
      (*id2index)[agg_children[i].child_id] = i;
    }
  };

  for (size_t i = 0; i < agg_children.size(); ++i) {
    lopri[i] = agg_children[i].currently_lopri;
    auto iter = last_is_lopri_.find(agg_children[i].child_id);
    if (iter != last_is_lopri_.end()) {
      lopri[i] = iter->second;
    }
  }

  // Downgrade (outside loop has few elements)
  for (IdRange range : diff.to_downgrade.ranges) {
    for (size_t i = 0; i < agg_children.size(); ++i) {
      if (range.Contains(agg_children[i].child_id)) {
        lopri[i] = true;
      }
    }
  }

  if (!diff.to_downgrade.points.empty()) {
    lazy_init_id2index();
    for (uint64_t point : diff.to_downgrade.points) {
      if (auto iter = id2index->find(point); iter != id2index->end()) {
        lopri[iter->second] = true;
      }
    }
  }

  // Upgrade (outside loop has few elements)
  for (IdRange range : diff.to_upgrade.ranges) {
    for (size_t i = 0; i < agg_children.size(); ++i) {
      if (range.Contains(agg_children[i].child_id)) {
        lopri[i] = false;
      }
    }
  }
  if (!diff.to_upgrade.points.empty()) {
    lazy_init_id2index();
    for (uint64_t point : diff.to_upgrade.points) {
      if (auto iter = id2index->find(point); iter != id2index->end()) {
        lopri[iter->second] = false;
      }
    }
  }

  for (size_t i = 0; i < agg_children.size(); ++i) {
    last_is_lopri_[agg_children[i].child_id] = lopri[i];
  }

  SPDLOG_LOGGER_INFO(logger, "picked LOPRI assignment: {}",
                     absl::StrJoin(lopri, "", BitmapFormatter()));

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
