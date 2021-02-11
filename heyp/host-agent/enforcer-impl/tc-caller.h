#ifndef HEYP_HOST_AGENT_ENFORCER_IMPL_TC_CALLER_H_
#define HEYP_HOST_AGENT_ENFORCER_IMPL_TC_CALLER_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "third_party/simdjson/simdjson.h"

namespace heyp {

class TcCaller {
 public:
  explicit TcCaller(const std::string& tc_name = "tc");

  absl::Status Call(const std::vector<std::string>& tc_args);
  simdjson::dom::element GetResult() const { return result_; }

 private:
  const std::string tc_name_;
  simdjson::dom::parser parser_;
  std::string buf_;
  simdjson::dom::element result_;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_ENFORCER_IMPL_TC_CALLER_H_
