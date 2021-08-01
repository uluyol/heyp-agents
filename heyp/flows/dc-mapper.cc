#include "heyp/flows/dc-mapper.h"

#include <algorithm>

#include "heyp/log/spdlog.h"

namespace heyp {

StaticDCMapper::StaticDCMapper(const proto::StaticDCMapperConfig& config) {
  for (const auto& entry : config.mapping().entries()) {
    host_addr_to_dc_[entry.host_addr()] = entry.dc();
    dc_to_all_hosts_[entry.dc()].push_back(entry.host_addr());
    if (std::find(all_dcs_.begin(), all_dcs_.end(), entry.dc()) == all_dcs_.end()) {
      all_dcs_.push_back(entry.dc());
    }
  }
}

const std::string* StaticDCMapper::HostDC(absl::string_view host) const {
  auto iter = host_addr_to_dc_.find(host);
  if (iter == host_addr_to_dc_.end()) {
    return nullptr;
  }
  return &iter->second;
}

const std::vector<std::string>* StaticDCMapper::HostsForDC(absl::string_view dc) const {
  auto iter = dc_to_all_hosts_.find(dc);
  if (iter == dc_to_all_hosts_.end()) {
    return nullptr;
  }
  return &iter->second;
}

const std::vector<std::string>& StaticDCMapper::AllDCs() const { return all_dcs_; }

}  // namespace heyp
