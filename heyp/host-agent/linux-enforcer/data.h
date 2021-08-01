#ifndef HEYP_HOST_AGENT_LINUX_ENFORCER_DATA_H_
#define HEYP_HOST_AGENT_LINUX_ENFORCER_DATA_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "spdlog/spdlog.h"

namespace heyp {

absl::StatusOr<std::string> FindDeviceResponsibleFor(
    const std::vector<std::string>& ip_addrs, spdlog::logger* logger,
    const std::string& ip_bin_name = "ip");

}

#endif  // HEYP_HOST_AGENT_LINUX_ENFORCER_DATA_H_
