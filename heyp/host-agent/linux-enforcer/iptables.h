// Iptables is an interface to control Linux iptables modeled after
// https://pkg.go.dev/k8s.io/kubernetes/pkg/util/iptables#Interface
//
// Derived from Kubernetes
// https://github.com/kubernetes/kubernetes/blob/v1.20.2/pkg/util/iptables/iptables.go
//
// Copyright 2014 The Kubernetes Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HEYP_HOST_AGENT_LINUX_ENFORCER_IPTABLES_H_
#define HEYP_HOST_AGENT_LINUX_ENFORCER_IPTABLES_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"

namespace heyp {
namespace iptables {

enum class Table {
  kNAT,
  kFilter,
  kMangle,
};

absl::string_view ToString(Table t);

enum class Chain {
  // kPostrouting used for source NAT in nat table
  kPostrouting,
  // kPrerouting used for DNAT (destination NAT) in nat table
  kPrerouting,
  // kOutput used for the packets going out from local
  kOutput,
  // kInput used for incoming packets
  kInput,
  // kForward used for the packets for another NIC
  kForward,
};

absl::string_view ToString(Chain c);

enum class RulePosition {
  kPrepend,
  kAppend,
};

absl::string_view ToString(RulePosition p);

enum class IpFamily {
  kIpV4,
  kIpV6,
};

absl::string_view ToString(IpFamily f);

enum class Operation {
  kCreateChain,
  kFlushChain,
  kDeleteChain,
  kListChain,
  kAppendRule,
  kCheckRule,
  kDeleteRule,

  // RulePosition
  kRulePositionPrepend,
  kRulePositionAppend,
};

absl::string_view ToString(Operation op);
Operation ToOp(RulePosition p);

class Runner {
 public:
  static std::unique_ptr<Runner> Create(IpFamily family);
  static std::unique_ptr<Runner> CreateWithIptablesCommands(
      IpFamily family, absl::string_view iptables_cmd,
      absl::string_view iptables_save_cmd, absl::string_view iptables_restore_cmd);

  // EnsureChain checks if the specified chain exists and, if not, creates it.
  // If the chain existed, return true.
  absl::StatusOr<bool> EnsureChain(Table table, Chain chain);

  // FlushChain clears the specified chain.  If the chain did not exist,
  // return error.
  absl::Status FlushChain(Table table, Chain chain);

  // DeleteChain deletes the specified chain.  If the chain did not exist,
  // return error.
  absl::Status DeleteChain(Table table, Chain chain);

  // EnsureRule checks if the specified rule is present and, if not, creates
  // it.  If the rule existed, return true.
  absl::StatusOr<bool> EnsureRule(RulePosition position, Table table, Chain chain,
                                  std::vector<std::string> args);

  // DeleteRule checks if the specified rule is present and, if so, deletes
  // it.
  absl::Status DeleteRule(Table table, Chain chain, std::vector<std::string> args);

  // Protocol returns the IP family this instance is managing,
  IpFamily ip_family() const;

  // SaveInto calls `iptables-save` for table and stores result in a given
  // buffer.
  absl::Status SaveInto(Table table, absl::Cord& buffer);

  struct RestoreFlags {
    bool flush_tables = true;
    bool restore_counters = false;
  };

  // Restore runs `iptables-restore` passing data through []byte.
  // table is the Table to restore
  // data should be formatted like the output of SaveInto()
  // flush sets the presence of the "--noflush" flag. see: FlushFlag
  // counters sets the "--counters" flag. see: RestoreCountersFlag
  absl::Status Restore(Table table, const absl::Cord& data, RestoreFlags flags);

  // RestoreAll is the same as Restore except that no table is specified.
  absl::Status RestoreAll(const absl::Cord& data, RestoreFlags flags);

 private:
  Runner(IpFamily family, absl::string_view iptables_cmd,
         absl::string_view iptables_save_cmd, absl::string_view iptables_restore_cmd,
         std::vector<std::string> wait_flag, std::vector<std::string> restore_wait_flag);

  absl::Status RestoreInternal(std::vector<std::string> args, const absl::Cord& data,
                               RestoreFlags flags);

  const IpFamily family_;
  const std::string iptables_cmd_;
  const std::string iptables_save_cmd_;
  const std::string iptables_restore_cmd_;
  const bool has_check_;
  std::vector<std::string> wait_flag_;
  std::vector<std::string> restore_wait_flag_;

  absl::Mutex mu_;

  absl::StatusOr<std::string> Run(Operation op, const std::vector<std::string>& args,
                                  int* exit_status = nullptr)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::StatusOr<bool> CheckRule(Table table, Chain chain,
                                 const std::vector<std::string>& args)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::StatusOr<bool> CheckRuleUsingCheck(std::vector<std::string> args)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
};

}  // namespace iptables
}  // namespace heyp

#endif  // HEYP_HOST_AGENT_LINUX_ENFORCER_IPTABLES_H_
