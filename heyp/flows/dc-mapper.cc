#include "heyp/flows/dc-mapper.h"

#include "glog/logging.h"

namespace heyp {

StaticDCMapper::StaticDCMapper(const proto::StaticDCMapperConfig& config) {
  for (const auto& entry : config.mapping().entries()) {
    host_addr_to_dc_[entry.host_addr()] = entry.dc();
  }
}

const std::string* StaticDCMapper::HostDC(absl::string_view host) const {
  auto iter = host_addr_to_dc_.find(host);
  if (iter == host_addr_to_dc_.end()) {
    return nullptr;
  }
  return &iter->second;
}

const std::vector<std::string>* StaticDCMapper::HostsForDC(
    absl::string_view dc) const {
  auto iter = dc_to_all_hosts_.find(dc);
  if (iter == dc_to_all_hosts_.end()) {
    return nullptr;
  }
  return &iter->second;
}

}  // namespace heyp
