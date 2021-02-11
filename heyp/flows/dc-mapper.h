#ifndef HEYP_FLOWS_DC_MAPPER_H_
#define HEYP_FLOWS_DC_MAPPER_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "heyp/proto/config.pb.h"

namespace heyp {

class DCMapper {
 public:
  virtual ~DCMapper() = default;

  virtual const std::string* HostDC(absl::string_view host) const = 0;
};

class StaticDCMapper : public DCMapper {
 public:
  explicit StaticDCMapper(const proto::StaticDCMapperConfig& config);

  const std::string* HostDC(absl::string_view host) const override;
  const std::vector<std::string>* HostsForDC(absl::string_view dc) const;

 private:
  absl::flat_hash_map<std::string, std::string> host_addr_to_dc_;
  absl::flat_hash_map<std::string, std::vector<std::string>> dc_to_all_hosts_;
};

}  // namespace heyp

#endif  // HEYP_FLOWS_DC_MAPPER_H_
