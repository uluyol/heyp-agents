#ifndef HEYP_CLI_PARSE_H_
#define HEYP_CLI_PARSE_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/proto/config.pb.h"

namespace heyp {

absl::StatusOr<absl::Duration> ParseAbslDuration(
    absl::string_view dur, absl::string_view field_name_on_error);

absl::Status ParseDemandPredictorConfig(
    const proto::DemandPredictorConfig &c,
    std::unique_ptr<DemandPredictor> *predictor, absl::Duration *time_window);

}  // namespace heyp

#endif  // HEYP_CLI_PARSE_H_
