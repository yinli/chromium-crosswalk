// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <linux/unistd.h>
#include <stdio.h>
#include <sys/prctl.h>

#include <string>

#include "sandbox/linux/seccomp-bpf/sandbox_bpf.h"
#include "sandbox/linux/seccomp-bpf/syscall.h"


namespace playground2 {

void Die::ExitGroup() {
  // exit_group() should exit our program. After all, it is defined as a
  // function that doesn't return. But things can theoretically go wrong.
  // Especially, since we are dealing with system call filters. Continuing
  // execution would be very bad in most cases where ExitGroup() gets called.
  // So, we'll try a few other strategies too.
  SandboxSyscall(__NR_exit_group, 1);

  // We have no idea what our run-time environment looks like. So, signal
  // handlers might or might not do the right thing. Try to reset settings
  // to a defined state; but we have not way to verify whether we actually
  // succeeded in doing so. Nonetheless, triggering a fatal signal could help
  // us terminate.
  signal(SIGSEGV, SIG_DFL);
  SandboxSyscall(__NR_prctl, PR_SET_DUMPABLE, (void *)0, (void *)0, (void *)0);
  if (*(volatile char *)0) { }

  // If there is no way for us to ask for the program to exit, the next
  // best thing we can do is to loop indefinitely. Maybe, somebody will notice
  // and file a bug...
  // We in fact retry the system call inside of our loop so that it will
  // stand out when somebody tries to diagnose the problem by using "strace".
  for (;;) {
    SandboxSyscall(__NR_exit_group, 1);
  }
}

void Die::SandboxDie(const char *msg, const char *file, int line) {
  if (simple_exit_) {
    LogToStderr(msg, file, line);
  } else {
    #if defined(SECCOMP_BPF_STANDALONE)
      Die::LogToStderr(msg, file, line);
    #else
      logging::LogMessage(file, line, logging::LOG_FATAL).stream() << msg;
    #endif
  }
  ExitGroup();
}

void Die::RawSandboxDie(const char *msg) {
  if (!msg)
    msg = "";
  RAW_LOG(FATAL, msg);
  ExitGroup();
}

void Die::SandboxInfo(const char *msg, const char *file, int line) {
  if (!suppress_info_) {
  #if defined(SECCOMP_BPF_STANDALONE)
    Die::LogToStderr(msg, file, line);
  #else
    logging::LogMessage(file, line, logging::LOG_INFO).stream() << msg;
  #endif
  }
}

void Die::LogToStderr(const char *msg, const char *file, int line) {
  if (msg) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%d", line);
    std::string s = std::string(file) + ":" + buf + ":" + msg + "\n";

    // No need to loop. Short write()s are unlikely and if they happen we
    // probably prefer them over a loop that blocks.
    if (HANDLE_EINTR(SandboxSyscall(__NR_write, 2, s.c_str(), s.length()))) { }
  }
}

bool Die::simple_exit_   = false;
bool Die::suppress_info_ = false;

}  // namespace
