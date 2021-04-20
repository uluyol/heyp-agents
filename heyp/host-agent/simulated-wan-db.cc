#include "heyp/host-agent/simulated-wan-db.h"

namespace heyp {

static std::string_view InternString(const std::string& s,
                                     std::vector<std::unique_ptr<std::string>>* table) {
  for (auto& np : *table) {
    if (*np == s) {
      return *np;
    }
  }
  table->push_back(std::make_unique<std::string>(s));
  return *table->back();
}

SimulatedWanDB::SimulatedWanDB(const proto::SimulatedWanConfig& config) {
  for (auto pair : config.dc_pairs()) {
    netem_configs_[{InternString(pair.src_dc(), &dc_names_),
                    InternString(pair.dst_dc(), &dc_names_)}] = pair.netem();
  }
}

const proto::NetemConfig* SimulatedWanDB::GetNetem(std::string_view src,
                                                   std::string_view dst) const {
  auto iter = netem_configs_.find({src, dst});
  if (iter == netem_configs_.end()) {
    return nullptr;
  }
  return &iter->second;
}

}  // namespace heyp