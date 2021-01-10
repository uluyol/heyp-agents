#ifndef HEYP_FLOWS_DC_MAPPER_H_
#define HEYP_FLOWS_DC_MAPPER_H_

#include <string>

#include "absl/strings/string_view.h"

namespace heyp {

class DCMapper {
 public:
  virtual ~DCMapper() = default;

  virtual std::string HostDC(absl::string_view host) const = 0;
};

class StaticDCMapper : public DCMapper {
 public:
  std::string HostDC(absl::string_view host) const override;
};

}  // namespace heyp

#endif  // HEYP_FLOWS_DC_MAPPER_H_
