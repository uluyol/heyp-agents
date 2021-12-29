#ifndef HEYP_HOST_AGENT_LINUX_ENFORCER_IPTABLES_CONTROLLER_H_
#define HEYP_HOST_AGENT_LINUX_ENFORCER_IPTABLES_CONTROLLER_H_

#include <cstdint>
#include <ostream>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "heyp/host-agent/linux-enforcer/iptables.h"
#include "heyp/host-agent/linux-enforcer/small-string-set.h"
#include "spdlog/spdlog.h"

namespace heyp {
namespace iptables {

struct SettingBatch {
  struct Setting {
    uint16_t src_port = 0;  // optional, 0 to ignore
    uint16_t dst_port = 0;  // optional, 0 to ignore
    std::string dst_addr;   // required
    std::string class_id;   // required
    std::string dscp;       // required
  };

  std::vector<Setting> settings;
};

// SettingsFindDscp returns the dscp iptables will use for the requested flow,
// or default_dscp otherwise.
absl::string_view SettingsFindDscp(const SettingBatch& batch, uint16_t src_port,
                                   uint16_t dst_port, absl::string_view dst_addr,
                                   absl::string_view default_dscp);

std::ostream& operator<<(std::ostream& os, const SettingBatch::Setting& s);

bool operator==(const SettingBatch::Setting& lhs, const SettingBatch::Setting& rhs);

bool operator<(const SettingBatch::Setting& lhs, const SettingBatch::Setting& rhs);

// ComputeDiff sorts old and new batches but otherwise leaves the contents
// alone. It will add any entries in new_batch that do not exist in old_batch
// and add them to to_add. Likewise it will populate to_del with entries from
// old_batch that are not in new_batch.
void ComputeDiff(SettingBatch& old_batch, SettingBatch& new_batch, SettingBatch* to_del,
                 SettingBatch* to_add);

// This interface is just used for mocking. See Controller for docs.
class ControllerIface {
 public:
  virtual ~ControllerIface() = default;
  virtual Runner& GetRunner() = 0;
  virtual absl::Status Clear() = 0;
  virtual void Stage(SettingBatch::Setting setting) = 0;
  virtual absl::Status CommitChanges() = 0;
  virtual const SettingBatch& AppliedSettings() const = 0;
};

class Controller : public ControllerIface {
 public:
  explicit Controller(absl::string_view dev, SmallStringSet dscps_to_ignore_class_id);

  Runner& GetRunner() override;

  absl::Status Clear() override;
  void Stage(SettingBatch::Setting setting) override;
  absl::Status CommitChanges() override;

  const SettingBatch& AppliedSettings() const override;

 private:
  const std::string dev_;
  const SmallStringSet dscps_to_ignore_class_id_;
  spdlog::logger logger_;
  std::unique_ptr<Runner> runner_;

  SettingBatch staged_;
  SettingBatch applied_;

  SettingBatch to_add_;
  SettingBatch to_del_;
};

// Exposed for testing

void AddRuleLinesToDelete(absl::string_view dev, const SettingBatch& batch,
                          absl::Cord& lines);
void AddRuleLinesToAdd(const SmallStringSet& dscp_to_ignore_class_id,
                       absl::string_view dev, const SettingBatch& batch,
                       absl::Cord& lines);

}  // namespace iptables
}  // namespace heyp

#endif  // HEYP_HOST_AGENT_LINUX_ENFORCER_IPTABLES_CONTROLLER_H_
