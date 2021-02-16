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

#ifndef HEYP_HOST_AGENT_ENFORCER_IMPL_IPTABLES_H_
#define HEYP_HOST_AGENT_ENFORCER_IMPL_IPTABLES_H_

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

  virtual ~Runner() = default;

  // EnsureChain checks if the specified chain exists and, if not, creates it.
  // If the chain existed, return true.
  virtual absl::StatusOr<bool> EnsureChain(Table table, Chain chain) = 0;

  // FlushChain clears the specified chain.  If the chain did not exist,
  // return error.
  virtual absl::Status FlushChain(Table table, Chain chain) = 0;

  // DeleteChain deletes the specified chain.  If the chain did not exist,
  // return error.
  virtual absl::Status DeleteChain(Table table, Chain chain) = 0;

  // EnsureRule checks if the specified rule is present and, if not, creates
  // it.  If the rule existed, return true.
  virtual absl::StatusOr<bool> EnsureRule(RulePosition position, Table table, Chain chain,
                                          std::vector<std::string> args) = 0;

  // DeleteRule checks if the specified rule is present and, if so, deletes
  // it.
  virtual absl::Status DeleteRule(Table table, Chain chain,
                                  std::vector<std::string> args) = 0;

  // Protocol returns the IP family this instance is managing,
  virtual IpFamily ip_family() const = 0;

  // SaveInto calls `iptables-save` for table and stores result in a given
  // buffer.
  virtual absl::Status SaveInto(Table table, absl::Cord& buffer) = 0;

  struct RestoreFlags {
    bool flush_tables = true;
    bool restore_counters = false;
  };

  // Restore runs `iptables-restore` passing data through []byte.
  // table is the Table to restore
  // data should be formatted like the output of SaveInto()
  // flush sets the presence of the "--noflush" flag. see: FlushFlag
  // counters sets the "--counters" flag. see: RestoreCountersFlag
  virtual absl::Status Restore(Table table, const absl::Cord& data,
                               RestoreFlags flags) = 0;

  // RestoreAll is the same as Restore except that no table is specified.
  virtual absl::Status RestoreAll(const absl::Cord& data, RestoreFlags flags) = 0;
};

}  // namespace iptables
}  // namespace heyp

#endif  // HEYP_HOST_AGENT_ENFORCER_IMPL_IPTABLES_H_
