#ifndef HEYP_HOST_AGENT_URLS_H_
#define HEYP_HOST_AGENT_URLS_H_

#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace heyp {

absl::Status ParseHostPort(absl::string_view s, absl::string_view *host, int32_t *port);

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_URLS_H_
