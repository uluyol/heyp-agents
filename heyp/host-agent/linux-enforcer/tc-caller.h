#ifndef HEYP_HOST_AGENT_LINUX_ENFORCER_TC_CALLER_H_
#define HEYP_HOST_AGENT_LINUX_ENFORCER_TC_CALLER_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "third_party/simdjson/simdjson.h"

namespace heyp {

class TcCaller {
 public:
  explicit TcCaller(const std::string& tc_name = "tc");

  absl::Status Call(const std::vector<std::string>& tc_args, bool parse_into_json);
  std::string RawOut() const { return buf_; };
  absl::optional<simdjson::dom::element> GetResult() const { return result_; }

 private:
  const std::string tc_name_;
  simdjson::dom::parser parser_;
  std::string buf_;
  absl::optional<simdjson::dom::element> result_;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_LINUX_ENFORCER_TC_CALLER_H_
