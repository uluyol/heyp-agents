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

  // Execute a batch of updates. The input should have a line for each tc command.
  //
  // Example:
  //   qdisc add dev eth1 root handle 1:0 htb default 10
  //   class add dev eth1 parent 1:0 classid 1:10 htb rate 1544kbit
  //   qdisc add dev eth1 parent 1:10 handle 10:0 netem delay 10ms
  // NOTE: lines do not begin with 'tc'.
  //
  // If force is set, tc will not stop at the first error. It will keep trying to apply
  // the requested changes.
  absl::Status Batch(const absl::Cord& input, bool force);

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
