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

  virtual std::string HostDC(absl::string_view host) const = 0;
};

class StaticDCMapper : public DCMapper {
 public:
  explicit StaticDCMapper(const proto::StaticDCMapperConfig& config);

  std::string HostDC(absl::string_view host) const override;

 private:
  absl::flat_hash_map<std::string, std::string> host_addr_to_dc_;
};

}  // namespace heyp

#endif  // HEYP_FLOWS_DC_MAPPER_H_
