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

#include "heyp/host-agent/enforcer-impl/iptables.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"
#include "boost/process/args.hpp"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "boost/process/pipe.hpp"
#include "boost/process/search_path.hpp"
#include "glog/logging.h"

namespace bp = boost::process;

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

namespace {

constexpr static char kIptablesSave[] = "iptables-save";
constexpr static char kIptablesRestore[] = "iptables-restore";
constexpr static char kIptables[] = "iptables";
constexpr static char kIp6tablesRestore[] = "ip6tables-restore";
constexpr static char kIp6tablesSave[] = "ip6tables-save";
constexpr static char kIp6tables[] = "ip6tables";

// WaitString a constant for specifying the wait flag
const char kWaitString[] = "-w";

// WaitSecondsValue a constant for specifying the default wait seconds
const char kWaitSecondsValue[] = "5";

// WaitIntervalString a constant for specifying the wait interval flag
const char kWaitIntervalString[] = "-W";

// WaitIntervalUsecondsValue a constant for specifying the default wait
// interval useconds
const char kWaitIntervalUsecondsValue[] = "100000";

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

}  // namespace

// runner implements Interface in terms of exec("iptables").
class RunnerImpl : public Runner {
 public:
  RunnerImpl(IpFamily family, std::vector<std::string> wait_flag,
             std::vector<std::string> restore_wait_flag)
      : family_(family),
        has_check_(true),  // assume recent-enough iptables
        wait_flag_(std::move(wait_flag)),
        restore_wait_flag_(std::move(restore_wait_flag)) {}

  // EnsureChain checks if the specified chain exists and, if not, creates it.
  // If the chain existed, return true.
  absl::StatusOr<bool> EnsureChain(Table table, Chain chain) override;

  // FlushChain clears the specified chain.  If the chain did not exist,
  // return error.
  absl::Status FlushChain(Table table, Chain chain) override;

  // DeleteChain deletes the specified chain.  If the chain did not exist,
  // return error.
  absl::Status DeleteChain(Table table, Chain chain) override;

  // EnsureRule checks if the specified rule is present and, if not, creates
  // it.  If the rule existed, return true.
  absl::StatusOr<bool> EnsureRule(RulePosition position, Table table,
                                  Chain chain,
                                  std::vector<std::string> args) override;

  // DeleteRule checks if the specified rule is present and, if so, deletes
  // it.
  absl::Status DeleteRule(Table table, Chain chain,
                          std::vector<std::string> args) override;

  // Protocol returns the IP family this instance is managing,
  IpFamily ip_family() const override { return family_; }

  // SaveInto calls `iptables-save` for table and stores result in a given
  // buffer.
  absl::Status SaveInto(Table table, std::string &buffer) override;

  // Restore runs `iptables-restore` passing data through []byte.
  // table is the Table to restore
  // data should be formatted like the output of SaveInto()
  // flush sets the presence of the "--noflush" flag. see: FlushFlag
  // counters sets the "--counters" flag. see: RestoreCountersFlag
  absl::Status Restore(Table table, const std::string &data,
                       RestoreFlags flags) override;

  // RestoreAll is the same as Restore except that no table is specified.
  absl::Status RestoreAll(const std::string &data, RestoreFlags flags) override;

 private:
  absl::Status RestoreInternal(std::vector<std::string> args,
                               const std::string &data, RestoreFlags flags);

  const IpFamily family_;
  const bool has_check_;
  std::vector<std::string> wait_flag_;
  std::vector<std::string> restore_wait_flag_;

  absl::Mutex mu_;

  absl::StatusOr<std::string> Run(Operation op,
                                  const std::vector<std::string> &args,
                                  int *exit_status = nullptr)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::StatusOr<bool> CheckRule(Table table, Chain chain,
                                 const std::vector<std::string> &args)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::StatusOr<bool> CheckRuleUsingCheck(std::vector<std::string> args)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
};

std::unique_ptr<Runner> Runner::Create(IpFamily family) {
  return absl::make_unique<RunnerImpl>(
      family,
      std::vector<std::string>{
          kWaitString, kWaitSecondsValue, kWaitIntervalString,
          kWaitIntervalUsecondsValue} /* assume recent-enough iptables */,
      std::vector<std::string>{
          kWaitString, kWaitSecondsValue, kWaitIntervalString,
          kWaitIntervalUsecondsValue} /* assume recent-enough iptables */);
}

namespace {

std::vector<std::string> MakeFullArgs(
    Table table, Chain chain, const std::vector<std::string> &args = {}) {
  std::vector<std::string> result{
      std::string(ToString(chain)),
      "-t",
      std::string(ToString(table)),
  };
  for (const std::string &arg : args) {
    result.push_back(arg);
  }
  return result;
}

}  // namespace

absl::StatusOr<std::string> RunnerImpl::Run(
    Operation op, const std::vector<std::string> &args, int *exit_status) {
  const char *iptables_cmd = kIptables;
  if (family_ == IpFamily::kIpV6) {
    iptables_cmd = kIp6tables;
  }
  std::vector<std::string> full_args = wait_flag_;
  full_args.push_back(std::string(ToString(op)));
  for (const std::string &arg : args) {
    full_args.push_back(arg);
  }
  VLOG(2) << "running iptables: " << iptables_cmd
          << absl::StrJoin(full_args, " ");

  try {
    bp::ipstream out_stream;
    bp::child c(bp::search_path(iptables_cmd), bp::args(full_args),
                bp::std_out > out_stream, bp::std_err > out_stream);

    std::string out;
    std::copy(std::istreambuf_iterator<char>(out_stream), {},
              std::back_inserter(out));

    c.wait();
    if (exit_status != nullptr) {
      *exit_status = c.exit_code();
    } else if (c.exit_code() != 0) {
      return absl::InternalError(
          absl::StrCat("iptables exit status ", c.exit_code()));
    }

    return out;
  } catch (const std::system_error &e) {
    return absl::InternalError(
        absl::StrCat("failed to run iptables subprocess: ", e.what()));
  }
}

absl::StatusOr<bool> RunnerImpl::EnsureChain(Table table, Chain chain) {
  std::vector<std::string> full_args = MakeFullArgs(table, chain);
  absl::MutexLock lock(&mu_);

  int exit_status = 0;
  auto st = Run(Operation::kCreateChain, full_args, &exit_status).status();
  if (st.ok() && exit_status == 1) {
    return true;
  }
  if (!st.ok()) {
    return absl::InternalError(absl::StrCat(
        "failed creating chain \"", ToString(chain), "\": ", st.message()));
  }
  return false;
}

absl::Status RunnerImpl::FlushChain(Table table, Chain chain) {
  std::vector<std::string> full_args = MakeFullArgs(table, chain);
  absl::MutexLock lock(&mu_);

  absl::Status st = Run(Operation::kFlushChain, full_args).status();
  if (!st.ok()) {
    st = absl::InternalError(absl::StrCat(
        "error flushing chain \"", ToString(chain), "\": ", st.message()));
  }
  return st;
}

absl::Status RunnerImpl::DeleteChain(Table table, Chain chain) {
  std::vector<std::string> full_args = MakeFullArgs(table, chain);
  absl::MutexLock lock(&mu_);

  // TODO: we could call iptables -S first, ignore the output and check for
  // non-zero return (more like DeleteRule)
  absl::Status st = Run(Operation::kDeleteChain, full_args).status();
  if (!st.ok()) {
    st = absl::InternalError(absl::StrCat(
        "error deleting chain \"", ToString(chain), "\": ", st.message()));
  }
  return st;
}

// Returns (bool, nil) if it was able to check the existence of the rule, or
// (<undefined>, error) if the process of checking failed.
absl::StatusOr<bool> RunnerImpl::CheckRule(
    Table table, Chain chain, const std::vector<std::string> &args) {
  if (has_check_) {
    return CheckRuleUsingCheck(MakeFullArgs(table, chain, args));
  }
  LOG(FATAL) << "CheckRunWithoutCheck is not implemented";
}

// Executes the rule check using the "-C" flag
absl::StatusOr<bool> RunnerImpl::CheckRuleUsingCheck(
    std::vector<std::string> args) {
  int exit_status = 0;
  absl::Status st = Run(Operation::kCheckRule, args).status();
  if (st.ok()) {
    if (exit_status == 0) {
      return true;
    } else {
      return false;
    }
  }
  return absl::InternalError(
      absl::StrCat("error checking rule: ", st.message()));
}

absl::StatusOr<bool> RunnerImpl::EnsureRule(RulePosition position, Table table,
                                            Chain chain,
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
    return absl::InternalError(
        absl::StrCat("error appending rule: ", st.message()));
  }
  return false;
}

absl::Status RunnerImpl::DeleteRule(Table table, Chain chain,
                                    std::vector<std::string> args) {
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
      st = absl::InternalError(
          absl::StrCat("error deleting rule: ", st.message()));
    }
  } else {
    // nothing to do, rule doesn't exist
  }
  return st;
}

absl::Status RunnerImpl::SaveInto(Table table, std::string &buffer) {
  const char *save_cmd = kIptablesSave;
  if (family_ == IpFamily::kIpV6) {
    save_cmd = kIp6tablesSave;
  }
  std::vector<std::string> args{"-t", std::string(ToString(table))};

  absl::MutexLock lock(&mu_);
  VLOG(2) << "running iptables save: " << save_cmd << absl::StrJoin(args, " ");

  try {
    bp::ipstream out_stream;
    bp::child c(bp::search_path(save_cmd), bp::args(args),
                bp::std_out > out_stream, bp::std_err > stderr);

    buffer.clear();
    std::copy(std::istreambuf_iterator<char>(out_stream), {},
              std::back_inserter(buffer));

    c.wait();
    if (c.exit_code() != 0) {
      return absl::InternalError(
          absl::StrCat("iptables save exit status ", c.exit_code()));
    }
    return absl::OkStatus();
  } catch (const std::system_error &e) {
    return absl::InternalError(
        absl::StrCat("failed to run iptables subprocess: ", e.what()));
  }
}

absl::Status RunnerImpl::Restore(Table table, const std::string &data,
                                 RestoreFlags flags) {
  return RestoreInternal({"-T", std::string(ToString(table))}, data, flags);
}

absl::Status RunnerImpl::RestoreAll(const std::string &data,
                                    RestoreFlags flags) {
  return RestoreInternal({}, data, flags);
}

// restoreInternal is the shared part of Restore/RestoreAll
absl::Status RunnerImpl::RestoreInternal(std::vector<std::string> args,
                                         const std::string &data,
                                         RestoreFlags flags) {
  if (!flags.flush_tables) {
    args.push_back("--noflush");
  }
  if (flags.restore_counters) {
    args.push_back("--counters");
  }
  std::vector<std::string> full_args = restore_wait_flag_;
  for (const std::string &arg : args) {
    full_args.push_back(arg);
  }

  const char *restore_cmd = kIptablesRestore;
  if (family_ == IpFamily::kIpV6) {
    restore_cmd = kIp6tablesRestore;
  }

  absl::MutexLock lock(&mu_);

  CHECK(!restore_wait_flag_.empty())
      << "support for older iptables-restore not implemented";

  try {
    bp::opstream input_stream;
    bp::child c(bp::search_path(restore_cmd), bp::args(full_args),
                bp::std_out > stdout, bp::std_err > stderr,
                bp::std_in < input_stream);

    input_stream << data;
    input_stream.close();

    c.wait();
    if (c.exit_code() != 0) {
      return absl::InternalError(
          absl::StrCat("iptables restore exit status ", c.exit_code()));
    }
    return absl::OkStatus();
  } catch (const std::system_error &e) {
    return absl::InternalError(
        absl::StrCat("failed to run iptables restore subprocess: ", e.what()));
  }
}

}  // namespace iptables
}  // namespace heyp
