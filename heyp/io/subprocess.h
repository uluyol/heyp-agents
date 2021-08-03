/* Derived from
https://github.com/tensorflow/tensorflow/blob/5dcfc51118817f27fad5246812d83e5dccdc5f72/tensorflow/core/platform/default/subprocess.h

Copyright 2016 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef TENSORFLOW_CORE_PLATFORM_POSIX_SUBPROCESS_H_
#define TENSORFLOW_CORE_PLATFORM_POSIX_SUBPROCESS_H_

#include <errno.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "absl/base/macros.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "spdlog/spdlog.h"

namespace heyp {

// Channel identifiers.
enum Channel {
  CHAN_STDIN = 0,
  CHAN_STDOUT = 1,
  CHAN_STDERR = 2,
};

// Specify how a channel is handled.
enum ChannelAction {
  // Close the file descriptor when the process starts.
  // This is the default behavior.
  ACTION_CLOSE,

  // Make a pipe to the channel.  It is used in the Communicate() method to
  // transfer data between the parent and child processes.
  ACTION_PIPE,

  // Duplicate the parent's file descriptor. Useful if stdout/stderr should
  // go to the same place that the parent writes it.
  ACTION_DUPPARENT,
};

// ExitStatus is a wrapper around an exit status to ensure that callers don't forget to
// check WIFEXITED.
class ExitStatus {
 public:
  ExitStatus(int val) : val_(val) {}

  int wait_status() const { return val_; }
  int exit_status() const;
  bool ok() const;

 private:
  int val_;
};

class SubProcess {
 public:
  // SubProcess()
  //    nfds: The number of file descriptors to use.
  explicit SubProcess(spdlog::logger* logger, int nfds = 3);

  // Virtual for backwards compatibility; do not create new subclasses.
  // It is illegal to delete the SubProcess within its exit callback.
  virtual ~SubProcess();

  // SetChannelAction()
  //    Set how to handle a channel.  The default action is ACTION_CLOSE.
  //    The action is set for all subsequent processes, until SetChannel()
  //    is called again.
  //
  //    SetChannel may not be called while the process is running.
  //
  //    chan: Which channel this applies to.
  //    action: What to do with the channel.
  // Virtual for backwards compatibility; do not create new subclasses.
  virtual void SetChannelAction(Channel chan, ChannelAction action);

  // SetProgram()
  //    Set up a program and argument list for execution, with the full
  //    "raw" argument list passed as a vector of strings.  argv[0]
  //    should be the program name, just as in execv().
  //
  //    file: The file containing the program.  This must be an absolute path
  //          name - $PATH is not searched.
  //    argv: The argument list.
  virtual void SetProgram(const std::string& file, const std::vector<std::string>& argv);

  // Start()
  //    Run the command that was previously set up with SetProgram().
  //    The following are fatal programming errors:
  //       * Attempting to start when a process is already running.
  //       * Attempting to start without first setting the command.
  //    Note, however, that Start() does not try to validate that the binary
  //    does anything reasonable (e.g. exists or can execute); as such, you can
  //    specify a non-existent binary and Start() will still return true.  You
  //    will get a failure from the process, but only after Start() returns.
  //
  //    Return true normally, or false if the program couldn't be started
  //    because of some error.
  // Virtual for backwards compatibility; do not create new subclasses.
  virtual bool Start();

  // KillAfter()
  //    Set a timeout for the process (after calling Start()).
  //    It will be sent SIGTERM.
  virtual void KillAfter(absl::Duration timeout);

  // Kill()
  //    Send the given signal to the process.
  //    Return true normally, or false if we couldn't send the signal - likely
  //    because the process doesn't exist.
  virtual bool Kill(int signal);

  // Wait()
  //    Block until the process exits.
  //    Return true normally, or false if the process wasn't running.
  virtual bool Wait();

  // Communicate()
  //    Read from stdout and stderr and writes to stdin until all pipes have
  //    closed, then waits for the process to exit.
  //    Note: Do NOT call Wait() after calling Communicate as it will always
  //     fail, since Communicate calls Wait() internally.
  //    'stdin_input', 'stdout_output', and 'stderr_output' may be NULL.
  //    If this process is not configured to send stdout or stderr to pipes,
  //     the output strings will not be modified.
  //    If this process is not configured to take stdin from a pipe, stdin_input
  //     will be ignored.
  //    Returns the command's exit status.
  virtual ExitStatus Communicate(const std::string* stdin_input,
                                 std::string* stdout_output, std::string* stderr_output);

 private:
  static constexpr int kNFds = 3;
  static bool chan_valid(int chan) { return ((chan >= 0) && (chan < kNFds)); }
  static bool retry(int e) {
    return ((e == EINTR) || (e == EAGAIN) || (e == EWOULDBLOCK));
  }
  void FreeArgs() ABSL_EXCLUSIVE_LOCKS_REQUIRED(data_mu_);
  void ClosePipes() ABSL_EXCLUSIVE_LOCKS_REQUIRED(data_mu_);
  bool WaitInternal(int* status);

  spdlog::logger* logger_;

  // The separation between proc_mu_ and data_mu_ mutexes allows Kill() to be
  // called by a thread while another thread is inside Wait() or Communicate().
  mutable absl::Mutex proc_mu_;
  bool running_ ABSL_GUARDED_BY(proc_mu_);
  pid_t pid_ ABSL_GUARDED_BY(proc_mu_);
  int timeout_pipe_[2] ABSL_GUARDED_BY(proc_mu_);
  absl::Notification* notify_done_;
  std::thread timeout_thread_;

  mutable absl::Mutex data_mu_ ABSL_ACQUIRED_AFTER(proc_mu_);
  char* exec_path_ ABSL_GUARDED_BY(data_mu_);
  char** exec_argv_ ABSL_GUARDED_BY(data_mu_);
  ChannelAction action_[kNFds] ABSL_GUARDED_BY(data_mu_);
  int parent_pipe_[kNFds] ABSL_GUARDED_BY(data_mu_);
  int child_pipe_[kNFds] ABSL_GUARDED_BY(data_mu_);

  SubProcess(const SubProcess&) = delete;
  SubProcess& operator=(const SubProcess&) = delete;
};

}  // namespace heyp

#endif  // TENSORFLOW_CORE_PLATFORM_POSIX_SUBPROCESS_H_