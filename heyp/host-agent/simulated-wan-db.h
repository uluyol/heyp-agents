#ifndef HEYP_HOST_AGENT_SIMULATED_WAN_DB_H_
#define HEYP_HOST_AGENT_SIMULATED_WAN_DB_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "heyp/flows/dc-mapper.h"
#include "heyp/proto/config.pb.h"

namespace heyp {

class SimulatedWanDB {
 public:
  explicit SimulatedWanDB(const proto::SimulatedWanConfig& config,
                          const StaticDCMapper& dc_mapper);

  struct QoSNetemConfig {
    std::optional<proto::NetemConfig> hipri;
    std::optional<proto::NetemConfig> lopri;

    std::string ToString() const;
  };

  const QoSNetemConfig* GetNetem(std::string_view src, std::string_view dst) const;

 private:
  std::vector<std::unique_ptr<std::string>> dc_names_;
  absl::flat_hash_map<std::pair<std::string_view, std::string_view>, QoSNetemConfig>
      netem_configs_;
};

}  // namespace heyp

#endif  // HEYP_HOST_AGENT_SIMULATED_WAN_DB_H_
