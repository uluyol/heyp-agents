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

#include "heyp/host-agent/linux-enforcer/iptables.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"
#include "heyp/io/subprocess.h"
#include "heyp/log/logging.h"

namespace heyp {
namespace iptables {

absl::string_view ToString(Table t) {
  switch (t) {
    case Table::kNAT:
      return "nat";
    case Table::kFilter:
      return "filter";
    case Table::kMangle:
      return "mangle";
  }
  return "unknown-table";
}

absl::string_view ToString(Chain c) {
  switch (c) {
    case Chain::kPostrouting:
      return "POSTROUTING";
    case Chain::kPrerouting:
      return "PREROUTING";
    case Chain::kOutput:
      return "OUTPUT";
    case Chain::kInput:
      return "INPUT";
    case Chain::kForward:
      return "FORWARD";
  }
  return "UNKNOWN-CHAIN";
}

absl::string_view ToString(RulePosition p) {
  switch (p) {
    case RulePosition::kPrepend:
      return "-I";
    case RulePosition::kAppend:
      return "-A";
  }
  return "--UNKNOWN-RULE-POSITON";
}

absl::string_view ToString(IpFamily f) {
  switch (f) {
    case IpFamily::kIpV4:
      return "IPv4";
    case IpFamily::kIpV6:
      return "IPv6";
  }
  return "UNKNOWN_IP_FAMILY";
}

absl::string_view ToString(Operation op) {
  switch (op) {
    case Operation::kCreateChain:
      return "-N";
    case Operation::kFlushChain:
      return "-F";
    case Operation::kDeleteChain:
      return "-X";
    case Operation::kListChain:
      return "-S";
    case Operation::kAppendRule:
      return "-A";
    case Operation::kCheckRule:
      return "-C";
    case Operation::kDeleteRule:
      return "-D";
    // RulePosition
    case Operation::kRulePositionPrepend:
      return ToString(RulePosition::kPrepend);
    case Operation::kRulePositionAppend:
      return ToString(RulePosition::kAppend);
  }
  return "--UNKNOWN-OPERATION";
}

Operation ToOp(RulePosition p) {
  switch (p) {
    case RulePosition::kPrepend:
      return Operation::kRulePositionPrepend;
    case RulePosition::kAppend:
      return Operation::kRulePositionAppend;
  }
  LOG(FATAL) << "unknown rule position";
}

namespace {

// WaitString a constant for specifying the wait flag
const char kWaitString[] = "-w";

// WaitSecondsValue a constant for specifying the default wait seconds
const char kWaitSecondsValue[] = "5";

// WaitIntervalString a constant for specifying the wait interval flag
const char kWaitIntervalString[] = "-W";

// WaitIntervalUsecondsValue a constant for specifying the default wait
// interval useconds
const char kWaitIntervalUsecondsValue[] = "100000";

}  // namespace

Runner::Runner(IpFamily family, absl::string_view iptables_cmd,
               absl::string_view iptables_save_cmd,
               absl::string_view iptables_restore_cmd, std::vector<std::string> wait_flag,
               std::vector<std::string> restore_wait_flag)
    : family_(family),
      iptables_cmd_(iptables_cmd),
      iptables_save_cmd_(iptables_save_cmd),
      iptables_restore_cmd_(iptables_restore_cmd),
      has_check_(true),  // assume recent-enough iptables
      wait_flag_(std::move(wait_flag)),
      restore_wait_flag_(std::move(restore_wait_flag)) {}

std::unique_ptr<Runner> Runner::Create(IpFamily family) {
  absl::string_view iptables_cmd = "iptables";
  absl::string_view iptables_save_cmd = "iptables-save";
  absl::string_view iptables_restore_cmd = "iptables-restore";

  if (family == IpFamily::kIpV6) {
    iptables_cmd = "ip6tables";
    iptables_save_cmd = "ip6tables-save";
    iptables_restore_cmd = "ip6tables-restore";
  }

  return CreateWithIptablesCommands(family, iptables_cmd, iptables_save_cmd,
                                    iptables_restore_cmd);
}

std::unique_ptr<Runner> Runner::CreateWithIptablesCommands(
    IpFamily family, absl::string_view iptables_cmd, absl::string_view iptables_save_cmd,
    absl::string_view iptables_restore_cmd) {
  return absl::WrapUnique(
      new Runner(family, iptables_cmd, iptables_save_cmd, iptables_restore_cmd,
                 std::vector<std::string>{
                     kWaitString, kWaitSecondsValue, kWaitIntervalString,
                     kWaitIntervalUsecondsValue} /* assume recent-enough iptables */,
                 std::vector<std::string>{
                     kWaitString, kWaitSecondsValue, kWaitIntervalString,
                     kWaitIntervalUsecondsValue} /* assume recent-enough iptables */));
}

namespace {

std::vector<std::string> MakeFullArgs(Table table, Chain chain,
                                      const std::vector<std::string>& args = {}) {
  std::vector<std::string> result{
      std::string(ToString(chain)),
      "-t",
      std::string(ToString(table)),
  };
  for (const std::string& arg : args) {
    result.push_back(arg);
  }
  return result;
}

}  // namespace

absl::StatusOr<std::string> Runner::Run(Operation op,
                                        const std::vector<std::string>& args,
                                        int* exit_status) {
  std::vector<std::string> full_args = wait_flag_;
  full_args.push_back(std::string(ToString(op)));
  for (const std::string& arg : args) {
    full_args.push_back(arg);
  }

  auto result = RunSubprocess(iptables_cmd_, full_args);
  if (!result.ok()) {
    return result.ErrorWhenRunning("iptables");
  }
  return std::string(result.out);
}

absl::StatusOr<bool> Runner::EnsureChain(Table table, Chain chain) {
  std::vector<std::string> full_args = MakeFullArgs(table, chain);
  absl::MutexLock lock(&mu_);

  int exit_status = 0;
  auto st = Run(Operation::kCreateChain, full_args, &exit_status).status();
  if (st.ok() && exit_status == 1) {
    return true;
  }
  if (!st.ok()) {
    return absl::InternalError(
        absl::StrCat("failed creating chain \"", ToString(chain), "\": ", st.message()));
  }
  return false;
}

absl::Status Runner::FlushChain(Table table, Chain chain) {
  std::vector<std::string> full_args = MakeFullArgs(table, chain);
  absl::MutexLock lock(&mu_);

  absl::Status st = Run(Operation::kFlushChain, full_args).status();
  if (!st.ok()) {
    st = absl::InternalError(
        absl::StrCat("error flushing chain \"", ToString(chain), "\": ", st.message()));
  }
  return st;
}

absl::Status Runner::DeleteChain(Table table, Chain chain) {
  std::vector<std::string> full_args = MakeFullArgs(table, chain);
  absl::MutexLock lock(&mu_);

  // TODO: we could call iptables -S first, ignore the output and check for
  // non-zero return (more like DeleteRule)
  absl::Status st = Run(Operation::kDeleteChain, full_args).status();
  if (!st.ok()) {
    st = absl::InternalError(
        absl::StrCat("error deleting chain \"", ToString(chain), "\": ", st.message()));
  }
  return st;
}

// Returns (bool, nil) if it was able to check the existence of the rule, or
// (<undefined>, error) if the process of checking failed.
absl::StatusOr<bool> Runner::CheckRule(Table table, Chain chain,
                                       const std::vector<std::string>& args) {
  if (has_check_) {
    return CheckRuleUsingCheck(MakeFullArgs(table, chain, args));
  }
  LOG(FATAL) << "CheckRunWithoutCheck is not implemented";
}

// Executes the rule check using the "-C" flag
absl::StatusOr<bool> Runner::CheckRuleUsingCheck(std::vector<std::string> args) {
  int exit_status = 0;
  absl::Status st = Run(Operation::kCheckRule, args).status();
  if (st.ok()) {
    if (exit_status == 0) {
      return true;
    } else {
      return false;
    }
  }
  return absl::InternalError(absl::StrCat("error checking rule: ", st.message()));
}

absl::StatusOr<bool> Runner::EnsureRule(RulePosition position, Table table, Chain chain,
                                        std::vector<std::string> args) {
  std::vector<std::string> full_args = MakeFullArgs(table, chain, args);
  absl::MutexLock lock(&mu_);

  auto exists_or = CheckRule(table, chain, args);
  if (!exists_or.ok()) {
    return exists_or.status();
  }
  if (exists_or.value()) {
    // already exists
    return true;
  }
  absl::Status st = Run(ToOp(position), full_args).status();
  if (!st.ok()) {
    return absl::InternalError(absl::StrCat("error appending rule: ", st.message()));
  }
  return false;
}

absl::Status Runner::DeleteRule(Table table, Chain chain, std::vector<std::string> args) {
  std::vector<std::string> full_args = MakeFullArgs(table, chain, args);
  absl::MutexLock lock(&mu_);

  auto exists_or = CheckRule(table, chain, args);
  if (!exists_or.ok()) {
    return exists_or.status();
  }
  absl::Status st = absl::OkStatus();
  if (exists_or.value()) {
    st = Run(Operation::kDeleteRule, full_args).status();
    if (!st.ok()) {
      st = absl::InternalError(absl::StrCat("error deleting rule: ", st.message()));
    }
  } else {
    // nothing to do, rule doesn't exist
  }
  return st;
}

absl::Status Runner::SaveInto(Table table, absl::Cord& buffer) {
  std::vector<std::string> args{"-t", std::string(ToString(table))};

  absl::MutexLock lock(&mu_);
  auto result = RunSubprocess(iptables_save_cmd_, args);
  if (!result.ok()) {
    return result.ErrorWhenRunning("iptables save");
  }

  buffer = result.out;
  return absl::OkStatus();
}

absl::Status Runner::Restore(Table table, const absl::Cord& data, RestoreFlags flags) {
  return RestoreInternal({"-T", std::string(ToString(table))}, data, flags);
}

absl::Status Runner::RestoreAll(const absl::Cord& data, RestoreFlags flags) {
  return RestoreInternal({}, data, flags);
}

// restoreInternal is the shared part of Restore/RestoreAll
absl::Status Runner::RestoreInternal(std::vector<std::string> args,
                                     const absl::Cord& data, RestoreFlags flags) {
  if (!flags.flush_tables) {
    args.push_back("--noflush");
  }
  if (flags.restore_counters) {
    args.push_back("--counters");
  }
  std::vector<std::string> full_args = restore_wait_flag_;
  for (const std::string& arg : args) {
    full_args.push_back(arg);
  }

  absl::MutexLock lock(&mu_);

  CHECK(!restore_wait_flag_.empty())
      << "support for older iptables-restore not implemented";

  auto result = RunSubprocess(iptables_restore_cmd_, full_args, data);
  if (!result.ok()) {
    if (!result.ok()) {
      return result.ErrorWhenRunning("iptables restore");
    }
  }
  return absl::OkStatus();
}

}  // namespace iptables
}  // namespace heyp
