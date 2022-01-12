#include "heyp/alg/downgrade/impl-hashing.h"

#include "absl/strings/str_join.h"
#include "heyp/alg/debug.h"
#include "heyp/alg/downgrade/formatters.h"

namespace heyp {

static std::string MatchString(const RingRanges& r) {
  if (r.a.Empty() && r.b.Empty()) {
    return "{}";
  }
  if (r.a.Empty()) {
    return ToString(r.b);
  }
  if (r.b.Empty()) {
    return ToString(r.a);
  }
  return absl::StrCat(ToString(r.a), "∪", ToString(r.b));
}

DowngradeDiff HashingDowngradeSelector::PickChildren(const AggInfoView& agg_info,
                                                     const double want_frac_lopri,
                                                     spdlog::logger* logger) {
  const bool should_debug = DebugQosAndRateLimitSelection();

  if (logger != nullptr && should_debug) {
    SPDLOG_LOGGER_INFO(logger, "parent: {}", agg_info.parent().DebugString());
    SPDLOG_LOGGER_INFO(logger, "children: {}",
                       absl::StrJoin(agg_info.children(), "\n", absl::StreamFormatter()));
    SPDLOG_LOGGER_INFO(logger, "initial lopri ring: {}", lopri_.ToString());
  }

  DowngradeDiff ret;
  RangeDiff d = lopri_.UpdateFrac(want_frac_lopri);
  std::vector<IdRange>* dst = nullptr;
  switch (d.type) {
    case RangeDiffType::kAdd:
      dst = &ret.to_downgrade.ranges;
      break;
    case RangeDiffType::kDel:
      dst = &ret.to_upgrade.ranges;
      break;
    default: {
      if (logger != nullptr) {
        H_SPDLOG_CHECK_MESG(logger, false,
                            "impossible branch: were new RangeDiffTypes added?")
      }
      break;
    }
  }

  if (!d.diff.a.Empty()) {
    dst->push_back(d.diff.a);
  }
  if (!d.diff.b.Empty()) {
    dst->push_back(d.diff.b);
  }

  RingRanges matching = lopri_.MatchingRanges();
  if (logger != nullptr) {
    SPDLOG_LOGGER_INFO(logger, "revised lopri ring: {} matches: {} downgrade diff: {}",
                       lopri_.ToString(), MatchString(matching), ToString(ret));
    if (should_debug) {
      SPDLOG_LOGGER_INFO(logger, "range diff: {}", ToString(d));
    }
  }

  return ret;
}

}  // namespace heyp
