#include "heyp/flows/dc-mapper.h"

#include "glog/logging.h"

namespace heyp {

StaticDCMapper::StaticDCMapper(const proto::StaticDCMapperConfig& config) {
  for (const auto& entry : config.mapping().entries()) {
    host_addr_to_dc_[entry.host_addr()] = entry.dc();
  }
}

std::string StaticDCMapper::HostDC(absl::string_view host) const {
  auto iter = host_addr_to_dc_.find(host);
  if (iter == host_addr_to_dc_.end()) {
    return "";
  }
  return iter->second;
}

}  // namespace heyp
