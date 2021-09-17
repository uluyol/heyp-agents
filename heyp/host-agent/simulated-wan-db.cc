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

SimulatedWanDB::SimulatedWanDB(const proto::SimulatedWanConfig& config,
                               const StaticDCMapper& dc_mapper) {
  for (auto pair : config.dc_pairs()) {
    QoSNetemConfig c;
    if (pair.has_netem()) {
      c.hipri = pair.netem();
    }
    if (pair.has_netem_lopri()) {
      c.lopri = pair.netem_lopri();
    } else if (pair.has_netem()) {
      c.lopri = c.hipri;
    }
    netem_configs_[{InternString(pair.src_dc(), &dc_names_),
                    InternString(pair.dst_dc(), &dc_names_)}] = c;
  }

  for (const std::string& src_dc_str : dc_mapper.AllDCs()) {
    std::string_view src_dc = InternString(src_dc_str, &dc_names_);
    for (const std::string& dst_dc : dc_mapper.AllDCs()) {
      auto key = std::make_pair(src_dc, InternString(dst_dc, &dc_names_));
      if (netem_configs_.find(key) == netem_configs_.end()) {
        netem_configs_[key] = QoSNetemConfig();
      }
    }
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
  return absl::StrCat(
      "{ hipri = ", hipri.has_value() ? hipri->ShortDebugString() : "unset",
      ", lopri = ", lopri.has_value() ? lopri->ShortDebugString() : "unset", " }");
}

}  // namespace heyp