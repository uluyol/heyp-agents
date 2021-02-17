#ifndef HEYP_INTEGRATION_HOST_AGENT_OS_TESTER_H_
#define HEYP_INTEGRATION_HOST_AGENT_OS_TESTER_H_

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "heyp/proto/integration.pb.h"

namespace heyp {
namespace testing {

class HostAgentOSTester {
 public:
  struct Config {
    std::string device;
    bool use_hipri = true;
    absl::Duration run_dur = absl::Seconds(60);
    absl::Duration step_dur = absl::Seconds(2);
    int num_hosts = 4;
    int64_t max_rate_limit_bps = 100 * 1024 * 1025;  // 100 Mbps
  };

  explicit HostAgentOSTester(Config config);

  absl::StatusOr<proto::TestCompareMetrics> Run();

 private:
  const Config config_;
};

}  // namespace testing
}  // namespace heyp

#endif  // HEYP_INTEGRATION_HOST_AGENT_OS_TESTER_H_
