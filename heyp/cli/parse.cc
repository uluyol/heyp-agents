#include "heyp/cli/parse.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "heyp/proto/config.pb.h"

namespace heyp {

absl::StatusOr<absl::Duration> ParseAbslDuration(
    absl::string_view dur, absl::string_view field_name_on_error) {
  absl::Duration d;
  if (!absl::ParseDuration(dur, &d)) {
    return absl::InvalidArgumentError(
        absl::StrCat("invalid ", field_name_on_error, ": ", dur));
  }
  return d;
}

absl::Status ParseDemandPredictorConfig(
    const proto::DemandPredictorConfig &c,
    std::unique_ptr<DemandPredictor> *predictor, absl::Duration *time_window) {
  auto window_or = ParseAbslDuration(c.time_window_dur(), "time_window");
  if (!window_or.ok()) {
    return window_or.status();
  }

  *time_window = *window_or;
  *predictor = absl::make_unique<BweDemandPredictor>(
      *time_window, c.usage_multiplier(), c.min_demand_bps());

  return absl::OkStatus();
}

}  // namespace heyp
