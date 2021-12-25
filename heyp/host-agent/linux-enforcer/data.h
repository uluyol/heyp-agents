#ifndef HEYP_HOST_AGENT_LINUX_ENFORCER_DATA_H_
#define HEYP_HOST_AGENT_LINUX_ENFORCER_DATA_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "spdlog/spdlog.h"

namespace heyp {

// Finds the device responsible the input ip addresses. Discovers this information using
// the JSON output from the "ip" command.
absl::StatusOr<std::string> FindDeviceResponsibleFor(
    const std::vector<std::string>& ip_addrs, spdlog::logger* logger,
    const std::string& ip_bin_name = "ip");

// Finds the device responsible the input ip addresses. Discovers this information on
// systems where there is no JSON output from the "ip" command. Less reliable since we're
// not 100% sure on the data format.
absl::StatusOr<std::string> FindDeviceResponsibleForLessReliable(
    const std::vector<std::string>& ip_addrs, spdlog::logger* logger,
    const std::string& ip_bin_name = "ip");

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_LINUX_ENFORCER_DATA_H_
