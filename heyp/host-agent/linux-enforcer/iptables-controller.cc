#include "heyp/host-agent/linux-enforcer/iptables-controller.h"

#include <algorithm>
#include <iterator>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "glog/logging.h"
#include "iptables.h"

namespace heyp {
namespace iptables {

using Setting = SettingBatch::Setting;

std::ostream& operator<<(std::ostream& os, const Setting& s) {
  return os << absl::StrFormat("Setting{%d, %d, %s, %s, %s}", s.src_port, s.dst_port,
                               s.dst_addr, s.class_id, s.dscp);
}

#define SAME_OR_RETURN(field_name)        \
  if (lhs.field_name != rhs.field_name) { \
    return false;                         \
  }

bool operator==(const Setting& lhs, const Setting& rhs) {
  SAME_OR_RETURN(src_port);
  SAME_OR_RETURN(dst_port);
  SAME_OR_RETURN(dst_addr);
  SAME_OR_RETURN(class_id);
  SAME_OR_RETURN(dscp);
  return true;
}

#undef SAME_OR_RETURN

#define COMPARE_FIELD(field_name)        \
  if (lhs.field_name < rhs.field_name) { \
    return true;                         \
  }                                      \
  if (lhs.field_name > rhs.field_name) { \
    return false;                        \
  }

bool operator<(const Setting& lhs, const Setting& rhs) {
  COMPARE_FIELD(src_port);
  COMPARE_FIELD(dst_port);
  COMPARE_FIELD(dst_addr);
  COMPARE_FIELD(class_id);
  COMPARE_FIELD(dscp);
  return false;
}

#undef COMPARE_FIELD

void ComputeDiff(SettingBatch& old_batch, SettingBatch& new_batch, SettingBatch* to_del,
                 SettingBatch* to_add) {
  std::sort(old_batch.settings.begin(), old_batch.settings.end());
  std::sort(new_batch.settings.begin(), new_batch.settings.end());

  const auto& old_vec = old_batch.settings;
  const auto& new_vec = new_batch.settings;

  // Populate to_del
  if (to_del != nullptr) {
    std::set_difference(old_vec.begin(), old_vec.end(), new_vec.begin(), new_vec.end(),
                        std::back_inserter(to_del->settings));
  }

  // Populate to_add
  if (to_add != nullptr) {
    std::set_difference(new_vec.begin(), new_vec.end(), old_vec.begin(), old_vec.end(),
                        std::back_inserter(to_add->settings));
  }
}

Controller::Controller(absl::string_view dev)
    : dev_(dev), runner_(Runner::Create(IpFamily::kIpV4)) {}

absl::Status Controller::Clear() {
  applied_.settings.clear();
  LOG(INFO) << "flushing iptables 'mangle' table";
  absl::Status st = runner_->Restore(Table::kMangle, absl::Cord(),
                                     {.flush_tables = true, .restore_counters = false});
  if (!st.ok()) {
    return absl::InternalError(
        absl::StrCat("failed to flush iptables 'mangle' table: ", st.message()));
  }
  return absl::OkStatus();
}

void Controller::Stage(SettingBatch::Setting setting) {
  staged_.settings.push_back(std::move(setting));
}

absl::Status Controller::CommitChanges() {
  ComputeDiff(applied_, staged_, &to_del_, &to_add_);
  applied_.settings.clear();

  absl::Cord mangle_table;

  // Uncomment if it turns out we need to save the table.
  //
  // LOG(INFO) << "Saving current 'mangle' table";
  // absl::Status st = runner_->SaveInto(Table::kMangle, mangle_table);
  // if (!st.ok()) {
  //   to_del_.settings.clear();
  //   to_add_.settings.clear();
  //   staged_.settings.clear();
  //   return absl::Status(
  //       st.code(),
  //       absl::StrCat("failed to save 'mangle' table rules: ", st.message()));
  // }

  AddRuleLinesToDelete(dev_, to_del_, mangle_table);
  AddRuleLinesToAdd(dev_, to_add_, mangle_table);
  mangle_table.Append("COMMIT\n");

  LOG(INFO) << "updating rules for iptables 'mangle' table";

  absl::Status st = runner_->Restore(Table::kMangle, mangle_table,
                                     {.flush_tables = false, .restore_counters = false});

  if (!st.ok()) {
    // We are in between the old and new states. Just make sure that next time
    // we rollback everything.
    std::copy(to_add_.settings.begin(), to_add_.settings.end(),
              std::back_inserter(to_del_.settings));
    to_add_.settings.clear();
    applied_.settings.clear();
    staged_.settings.clear();
    return absl::InternalError(
        absl::StrCat("failed to update iptables 'mangle' table: ", st.message()));
  }

  to_del_.settings.clear();
  to_add_.settings.clear();
  applied_.settings = staged_.settings;
  staged_.settings.clear();

  return absl::OkStatus();
}

void AddRuleLinesToDelete(absl::string_view dev, const SettingBatch& batch,
                          absl::Cord& lines) {
  std::string src_port_match;
  std::string dst_port_match;
  for (const Setting& s : batch.settings) {
    if (s.src_port != 0) {
      src_port_match = absl::StrCat(" --sport ", s.src_port);
    } else {
      src_port_match.clear();
    }
    if (s.dst_port != 0) {
      dst_port_match = absl::StrCat(" --dport ", s.dst_port);
    } else {
      dst_port_match.clear();
    }
    lines.Append(absl::StrFormat(
        "-D OUTPUT -o %s -p tcp -m tcp -d %s%s%s -j CLASSIFY --set-class %s\n", dev,
        s.dst_addr, src_port_match, dst_port_match, s.class_id));
    lines.Append(absl::StrFormat(
        "-D OUTPUT -o %s -p tcp -m tcp -d %s%s%s -j DSCP --set-dscp-class %s\n", dev,
        s.dst_addr, src_port_match, dst_port_match, s.dscp));
  }
}

void AddRuleLinesToAdd(absl::string_view dev, const SettingBatch& batch,
                       absl::Cord& lines) {
  std::string src_port_match;
  std::string dst_port_match;
  for (const Setting& s : batch.settings) {
    if (s.src_port != 0) {
      src_port_match = absl::StrCat(" --sport ", s.src_port);
    } else {
      src_port_match.clear();
    }
    if (s.dst_port != 0) {
      dst_port_match = absl::StrCat(" --dport ", s.dst_port);
    } else {
      dst_port_match.clear();
    }
    lines.Append(absl::StrFormat(
        "-A OUTPUT -o %s -p tcp -m tcp -d %s%s%s -j CLASSIFY --set-class %s\n", dev,
        s.dst_addr, src_port_match, dst_port_match, s.class_id));
    lines.Append(absl::StrFormat(
        "-A OUTPUT -o %s -p tcp -m tcp -d %s%s%s -j DSCP --set-dscp-class %s\n", dev,
        s.dst_addr, src_port_match, dst_port_match, s.dscp));
  }
}

}  // namespace iptables
}  // namespace heyp
