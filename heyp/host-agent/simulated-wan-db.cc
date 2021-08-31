#include "heyp/host-agent/simulated-wan-db.h"

#include "absl/strings/str_cat.h"

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
    QoSNetemConfig c;
    c.hipri = pair.netem();
    if (pair.has_netem_lopri()) {
      c.lopri = pair.netem_lopri();
    } else {
      c.lopri = c.hipri;
    }
    netem_configs_[{InternString(pair.src_dc(), &dc_names_),
                    InternString(pair.dst_dc(), &dc_names_)}] = c;
  }
}

const SimulatedWanDB::QoSNetemConfig* SimulatedWanDB::GetNetem(
    std::string_view src, std::string_view dst) const {
  auto iter = netem_configs_.find({src, dst});
  if (iter == netem_configs_.end()) {
    return nullptr;
  }
  return &iter->second;
}

std::string SimulatedWanDB::QoSNetemConfig::ToString() const {
  return absl::StrCat("{ hipri = ", hipri.ShortDebugString(),
                      ", lopri = ", lopri.ShortDebugString(), " }");
}

}  // namespace heyp