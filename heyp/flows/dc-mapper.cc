#include "heyp/flows/dc-mapper.h"

#include "glog/logging.h"

namespace heyp {

std::string StaticDCMapper::HostDC(absl::string_view host) const {
  LOG(INFO) << "StaticDCMapper not implemented";
  return "";
}

}  // namespace heyp
