/* Copyright 2016 The TensorFlow Authors. All Rights Reserved.

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

#include "heyp/io/subprocess.h"

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include <algorithm>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/log/spdlog.h"

namespace heyp {
namespace {

static spdlog::logger* TestLogger() {
  static spdlog::logger* logger = new spdlog::logger(MakeLogger("test"));
  return logger;
}

std::string EchoProgram() { return "heyp/io/testdata/test_echo"; }
std::string EchoArgv1Program() { return "heyp/io/testdata/test_echo_argv_1"; }
std::string NoopProgram() { return "heyp/io/testdata/test_noop"; }
std::string StdErrProgram() { return "heyp/io/testdata/test_stderr"; }

TEST(SubProcessTest, NoOutputNoComm) {
  SubProcess proc(TestLogger());
  proc.SetProgram(NoopProgram(), {});
  EXPECT_TRUE(proc.Start());
  EXPECT_TRUE(proc.Wait());
}

TEST(SubProcessTest, NoOutput) {
  SubProcess proc(TestLogger());
  proc.SetProgram(NoopProgram(), {});
  proc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  proc.SetChannelAction(CHAN_STDERR, ACTION_PIPE);
  EXPECT_TRUE(proc.Start());

  std::string out, err;
  int status = proc.Communicate(nullptr, &out, &err).wait_status();
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
  EXPECT_EQ("", out);
  EXPECT_EQ("", err);
}

TEST(SubProcessTest, Stdout) {
  SubProcess proc(TestLogger());
  const char test_string[] = "hello_world";
  proc.SetProgram(EchoArgv1Program(), {test_string});
  proc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  proc.SetChannelAction(CHAN_STDERR, ACTION_PIPE);
  EXPECT_TRUE(proc.Start());

  std::string out, err;
  int status = proc.Communicate(nullptr, &out, &err).wait_status();
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
  EXPECT_EQ(test_string, out);
  EXPECT_EQ("", err);
}

TEST(SubProcessTest, StdoutIgnored) {
  SubProcess proc(TestLogger());
  const char test_string[] = "hello_world";
  proc.SetProgram(EchoArgv1Program(), {test_string});
  proc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  proc.SetChannelAction(CHAN_STDERR, ACTION_PIPE);
  EXPECT_TRUE(proc.Start());

  int status = proc.Communicate(nullptr, nullptr, nullptr).wait_status();
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(SubProcessTest, Stderr) {
  SubProcess proc(TestLogger());
  const char test_string[] = "muh_failure!";
  proc.SetProgram(StdErrProgram(), {test_string});
  proc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  proc.SetChannelAction(CHAN_STDERR, ACTION_PIPE);
  EXPECT_TRUE(proc.Start());

  std::string out, err;
  int status = proc.Communicate(nullptr, &out, &err).wait_status();
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_NE(0, WEXITSTATUS(status));
  EXPECT_EQ("", out);
  EXPECT_EQ(test_string, err);
}

TEST(SubProcessTest, StderrIgnored) {
  SubProcess proc(TestLogger());
  const char test_string[] = "muh_failure!";
  proc.SetProgram(StdErrProgram(), {test_string});
  proc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  proc.SetChannelAction(CHAN_STDERR, ACTION_PIPE);
  EXPECT_TRUE(proc.Start());

  int status = proc.Communicate(nullptr, nullptr, nullptr).wait_status();
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_NE(0, WEXITSTATUS(status));
}

TEST(SubProcessTest, Stdin) {
  SubProcess proc(TestLogger());
  proc.SetProgram(EchoProgram(), {});
  proc.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
  EXPECT_TRUE(proc.Start());

  std::string in = "foobar\nbarfoo\nhaha\n";
  int status = proc.Communicate(&in, nullptr, nullptr).wait_status();
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(SubProcessTest, StdinStdout) {
  SubProcess proc(TestLogger());
  proc.SetProgram(EchoProgram(), {});
  proc.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
  proc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  EXPECT_TRUE(proc.Start());

  std::string in = "foobar\nbarfoo\nhaha\n";
  std::string out;
  int status = proc.Communicate(&in, &out, nullptr).wait_status();
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
  // Sanitize out of carriage returns, because windows...
  out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
  EXPECT_EQ(in, out);
}

TEST(SubProcessTest, StdinChildExit) {
  SubProcess proc(TestLogger());
  proc.SetProgram(NoopProgram(), {});
  proc.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
  EXPECT_TRUE(proc.Start());

  // Verify that the parent handles the child exiting immediately as the
  // parent is trying to write a large string to the child's stdin.
  std::string in;
  in.reserve(1000000);
  for (int i = 0; i < 100000; i++) {
    in += "hello xyz\n";
  }

  int status = proc.Communicate(&in, nullptr, nullptr).wait_status();
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(SubProcessTest, StdinStdoutOverlap) {
  SubProcess proc(TestLogger());
  proc.SetProgram(EchoProgram(), {});
  proc.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
  proc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  EXPECT_TRUE(proc.Start());

  // Verify that the parent handles multiplexed reading/writing to the child
  // process.  The string is large enough to exceed the buffering of the pipes.
  std::string in;
  in.reserve(1000000);
  for (int i = 0; i < 100000; i++) {
    in += "hello xyz\n";
  }

  std::string out;
  int status = proc.Communicate(&in, &out, nullptr).wait_status();
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
  // Sanitize out of carriage returns, because windows...
  out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
  EXPECT_EQ(in, out);
}

TEST(SubProcessTest, KillProc) {
  SubProcess proc(TestLogger());
  proc.SetProgram(EchoProgram(), {});
  proc.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
  proc.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  EXPECT_TRUE(proc.Start());

  EXPECT_TRUE(proc.Kill(SIGKILL));
  EXPECT_TRUE(proc.Wait());

  EXPECT_FALSE(proc.Kill(SIGKILL));
}

}  // namespace
}  // namespace heyp
