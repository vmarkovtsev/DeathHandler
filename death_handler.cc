/*

  Copyright (c) 2012, Samsung R&D Institute Russia
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met: 

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer. 
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution. 

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  The views and conclusions contained in the software and documentation are those
  of the authors and should not be interpreted as representing official policies, 
  either expressed or implied, of the FreeBSD Project.
  
 */

/*! @file death_handler.cc
 *  @brief Implementation of the SIGSEGV handler which prints the debug stack
 *  trace.
 *  @author Markovtsev Vadim <v.markovtsev@samsung.com>
 *  @version 1.0
 *  @license Simplified BSD License
 *  @copyright 2012 Samsung R&D Institute Russia
 */

#include "death_handler.h"
#include <assert.h>
#include <cxxabi.h>
#include <execinfo.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>

#pragma GCC poison malloc realloc free backtrace_symbols \
  printf fprintf sprintf snprintf scanf sscanf  // NOLINT(runtime/printf)

#define checked(x) do { if ((x) <= 0) abort(); } while (false)

namespace Debug {

/// @brief This namespace contains some basic supplements
/// of the needed libc functions which potentially use heap.
namespace Safe {
  /// @brief Converts an integer to a statically allocated string.
  char *itoa(int val, int base = 10) {
    static char res[32];
    int i;
    bool negative = val < 0;
    res[sizeof(res) - 1] = 0;
    for (i = sizeof(res) - 2; val != 0 && i != 0; i--, val /= base) {
      res[i] = "0123456789ABCDEF"[val % base];
    }
    if (negative) {
      res[i--] = '-';
    }
    return &res[i + 1];
  }

  /// @brief Converts an unsigned integer to a statically allocated string.
  char *utoa(uint64_t val, int base = 10) {
    static char res[32];
    int i;
    res[sizeof(res) - 1] = 0;
    for (i = sizeof(res) - 2; val != 0 && i != 0; i--, val /= base) {
      res[i] = "0123456789ABCDEF"[val % base];
    }
    return &res[i + 1];
  }

  /// @brief Converts a pointer to a statically allocated string.
  char *ptoa(const void *val) {
    char* buf = utoa(reinterpret_cast<uint64_t>(val), 16);
    static char result[32];
    strcpy(result + 2, buf);  // NOLINT(runtime/printf
    result[0] = '0';
    result[1] = 'x';
    return result;
  }

  /// @brief Reentrant printing to stderr.
  void print2stderr(const char *msg, size_t len = 0) {
    if (len > 0) {
      checked(write(STDERR_FILENO, msg, len));
    } else {
      checked(write(STDERR_FILENO, msg, strlen(msg)));
    }
  }
}  // namespace Safe

bool DeathHandler::generateCoreDump_ = true;
bool DeathHandler::cleanup_ = true;
#ifdef QUICK_EXIT
bool DeathHandler::quickExit_ = false;
#endif
int DeathHandler::framesCount_ = 16;
bool DeathHandler::cutCommonPathRoot_ = true;
bool DeathHandler::cutRelativePaths_ = true;
bool DeathHandler::appendPid_ = false;
bool DeathHandler::colorOutput_ = true;
bool DeathHandler::threadSafe_ = true;

DeathHandler::DeathHandler() {
  struct sigaction sa;
  sa.sa_handler = (__sighandler_t)SignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_SIGINFO;
  sigaction(SIGSEGV, &sa, NULL);
}

DeathHandler::~DeathHandler() {
  struct sigaction sa;
  sigaction(SIGSEGV, NULL, &sa);
  sa.sa_handler = SIG_DFL;
  sigaction(SIGSEGV, &sa, NULL);
}

bool DeathHandler::generateCoreDump() {
  return generateCoreDump_;
}

void DeathHandler::set_generateCoreDump(bool value) {
  generateCoreDump_ = value;
}

bool DeathHandler::cleanup() {
  return cleanup_;
}

void DeathHandler::set_cleanup(bool value) {
  cleanup_ = value;
}

#ifdef QUICK_EXIT
bool DeathHandler::quickExit() {
  return quickExit_;
}

void DeathHandler::set_quickExit(bool value) {
  quickExit_ = value;
}
#endif

int DeathHandler::framesCount() {
  return framesCount_;
}

void DeathHandler::set_framesCount(int value) {
  assert(value > 0 && value <= 100);
  framesCount_ = value;
}

bool DeathHandler::cutCommonPathRoot() {
  return cutCommonPathRoot_;
}

void DeathHandler::set_cutCommonPathRoot(bool value) {
  cutCommonPathRoot_ = value;
}

bool DeathHandler::cutRelativePaths() {
  return cutRelativePaths_;
}

void DeathHandler::set_cutRelativePaths(bool value) {
  cutRelativePaths_ = value;
}

bool DeathHandler::appendPid() {
  return appendPid_;
}

void DeathHandler::set_appendPid(bool value) {
  appendPid_ = value;
}

bool DeathHandler::colorOutput() {
  return colorOutput_;
}

void DeathHandler::set_colorOutput(bool value) {
  colorOutput_ = value;
}

bool DeathHandler::threadSafe() {
  return threadSafe_;
}

void DeathHandler::set_threadSafe(bool value) {
  threadSafe_ = value;
}

/// @brief Invokes addr2line utility to determine the function name
/// and the line information from an address in the code segment.
static char *addr2line(const char *image, void *addr) {
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    assert(execlp("addr2line", "addr2line",
                  Safe::ptoa(addr), "-f", "-C", "-e", image,
                  reinterpret_cast<void*>(NULL)) != -1);
  }

  close(pipefd[1]);
  static char line[4096];
  ssize_t len = read(pipefd[0], line, sizeof(line));
  close(pipefd[0]);
  line[len] = 0;

  assert(waitpid(pid, NULL, 0) == pid);
  if (line[0] == '?') {
    char* straddr = Safe::ptoa(addr);
    strcpy(line, "\033[32;1m");  // NOLINT(runtime/printf)
    strcat(line, straddr);  // NOLINT(runtime/printf)
    strcat(line, "\033[0m at ");  // NOLINT(runtime/printf)
    strcat(line, image);  // NOLINT(runtime/printf)
    strcat(line, " ");  // NOLINT(runtime/printf)
  } else {
    if (*(strstr(line, "\n") + 1) == '?') {
      char* straddr = Safe::ptoa(addr);
      strcpy(strstr(line, "\n") + 1, image);  // NOLINT(runtime/printf)
      strcat(line, ":");  // NOLINT(runtime/printf)
      strcat(line, straddr);  // NOLINT(runtime/printf)
      strcat(line, "\n");  // NOLINT(runtime/printf)
    }
  }
  return line;
}

/// @brief Used to workaround backtrace() usage of malloc().
static void* MallocHook(size_t size,
                        const void* caller __attribute__((unused))) {
  static char mallocBuffer[512];
  if (size > sizeof(mallocBuffer)) {
    const char* msg = "malloc() replacement function cannot return "
        "a memory block larger than 512 bytes\n";
    Safe::print2stderr(msg, strlen(msg) + 1);
    _Exit(EXIT_FAILURE);
  }
  return mallocBuffer;
}

#if __GNUC__ >= 4 && __GNUC_MINOR__ >= 6
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

void DeathHandler::SignalHandler(int sig __attribute__((unused)),
                                 void *info __attribute__((unused)),
                                 void *secret) {
  // Stop all other running threads by forking
  pid_t forkedPid = fork();
  if (forkedPid != 0) {
    int status;
    if (threadSafe_) {
      // Freeze the original process, until it's child prints the stack trace
      kill(getpid(), SIGSTOP);
      // Wait for the child without blocking and exit as soon as possible,
      // so that no zombies are left.
      waitpid(forkedPid, &status, WNOHANG);
    } else {
      // Wait for the child, blocking only the current thread.
      /// All other threads will continue to run, potentially
      /// early crashing the parent.
      waitpid(forkedPid, &status, 0);
    }
#ifdef QUICK_EXIT
    if (quickExit_) {
      quick_exit(EXIT_FAILURE);
    }
#endif
    if (generateCoreDump_) {
      abort();
    } else {
      if (cleanup_) {
        exit(EXIT_FAILURE);
      } else {
        _Exit(EXIT_FAILURE);
      }
    }
  }

  ucontext_t *uc = reinterpret_cast<ucontext_t *>(secret);

  if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1) {  // redirect stdout to stderr
    Safe::print2stderr("Failed to redirect stdout to stderr\n");
  }
  {
    static char msg[128];
    if (colorOutput_) {
      // \033[31;1mSegmentation fault\033[0m \033[33;1m(%i)\033[0m\n
      strcpy(msg,  // NOLINT(runtime/printf)
             "\033[31;1mSegmentation fault\033[0m (thread \033[33;1m");
      strcat(msg, Safe::utoa(pthread_self()));  // NOLINT(runtime/printf)
      strcat(msg, "\033[0m, pid \033[33;1m");  // NOLINT(runtime/printf)
      strcat(msg, Safe::itoa(getppid()));  // NOLINT(runtime/printf)
      strcat(msg, "\033[0m)");  // NOLINT(runtime/printf)
    } else {
      strcpy(msg, "Segmentation fault (thread ");  // NOLINT(runtime/printf)
      strcat(msg, Safe::utoa(pthread_self()));  // NOLINT(runtime/printf)
      strcat(msg, ", pid ");  // NOLINT(runtime/printf)
      strcat(msg, Safe::itoa(getppid()));  // NOLINT(runtime/printf)
      strcat(msg, ")");  // NOLINT(runtime/printf)
    }
    Safe::print2stderr(msg);
  }

  Safe::print2stderr("\nStack trace:\n");
  void *trace[framesCount_ + 2];
  // Workaround malloc() inside backtrace()
  void* (*oldMallocHook)(size_t, const void*) = __malloc_hook;
  void (*oldFreeHook)(void *, const void *) = __free_hook;
  __malloc_hook = MallocHook;
  __free_hook = NULL;
  int trace_size = backtrace(trace, framesCount_ + 2);
  __malloc_hook = oldMallocHook;
  __free_hook = oldFreeHook;
  if (trace_size <= 2) abort();

  // Overwrite sigaction with caller's address
#if defined(__arm__)
  trace[1] = reinterpret_cast<void *>(uc->uc_mcontext.arm_pc);
#else
#if !defined(__i386__) && !defined(__x86_64__)
#error Only ARM, x86 and x86-64 are supported
#endif
#if defined(__x86_64__)
  trace[1] = reinterpret_cast<void *>(uc->uc_mcontext.gregs[REG_RIP]);
#else
  trace[1] = reinterpret_cast<void *>(uc->uc_mcontext.gregs[REG_EIP]);
#endif
#endif

  char name_buf[1024];
  name_buf[readlink("/proc/self/exe", name_buf, sizeof(name_buf) - 1)] = 0;
  char cwd[1024];
  assert(getcwd(cwd, sizeof(cwd)) != NULL);
  assert(static_cast<size_t>(strlen(cwd)) < sizeof(cwd) - 1);
  strcat(cwd, "/");  // NOLINT(runtime/printf)

  int stackOffset = trace[2] == trace[1]? 2 : 1;
  for (int i = stackOffset; i < trace_size; i++) {
    char *line;
    Dl_info dlinf;
    if (dladdr(trace[i], &dlinf) == 0 ||
        !strcmp(name_buf, dlinf.dli_fname)) {
      line = addr2line(name_buf, trace[i]);
    } else {
      line = addr2line(dlinf.dli_fname, reinterpret_cast<void *>(
          reinterpret_cast<char *>(trace[i]) -
          reinterpret_cast<char *>(dlinf.dli_fbase)));
    }

    char *functionNameEnd = strstr(line, "\n");
    if (functionNameEnd != NULL) {
      *functionNameEnd = 0;
      {
        // "\033[34;1m[%s]\033[0m \033[33;1m(%i)\033[0m\n
        static char msg[512];
        msg[0] = 0;
        if (colorOutput_) {
          strcpy(msg, "\033[34;1m");  // NOLINT(runtime/printf)
        }
        strcat(msg, "[");  // NOLINT(runtime/printf)
        strcat(msg, line);  // NOLINT(runtime/printf)
        strcat(msg, "]");  // NOLINT(runtime/printf)
        if (appendPid_) {
          if (colorOutput_) {
            strcat(msg, "\033[0m\033[33;1m");  // NOLINT(runtime/printf)
          }
          strcat(msg, " (");  // NOLINT(runtime/printf)
          strcat(msg, Safe::itoa(getppid()));  // NOLINT(runtime/printf)
          strcat(msg, ")");  // NOLINT(runtime/printf)
          if (colorOutput_) {
            strcat(msg, "\033[0m");  // NOLINT(runtime/printf)
          }
          strcat(msg, "\n");  // NOLINT(runtime/printf)
        } else {
          if (colorOutput_) {
            strcat(msg, "\033[0m");  // NOLINT(runtime/printf)
          }
          strcat(msg, "\n");  // NOLINT(runtime/printf)
        }
        Safe::print2stderr(msg);
      }
      line = functionNameEnd + 1;

      // Remove the common path root
      if (cutCommonPathRoot_) {
        int cpi;
        for (cpi = 0; cwd[cpi] == line[cpi]; cpi++);
        if (cpi > 1) {
          line = line + cpi;
        }
      }

      // Remove relative path root
      if (cutRelativePaths_) {
        char *pathCutPos = strstr(line, "../");
        if (pathCutPos != NULL) {
          pathCutPos += 3;
          while (!strncmp(pathCutPos, "../", 3)) {
            pathCutPos += 3;
          }
          line = pathCutPos;
        }
      }

      // Mark line number
      if (colorOutput_) {
        char* numberPos = strstr(line, ":");
        if (numberPos != NULL) {
          static char lineNumber[128];
          strcpy(lineNumber, numberPos);  // NOLINT(runtime/printf)
          // Overwrite the new line char
          lineNumber[strlen(lineNumber) - 1] = 0;
          // \033[32;1m%s\033[0m\n
          strcpy(numberPos, "\033[32;1m");  // NOLINT(runtime/printf)
          strcat(line, lineNumber);  // NOLINT(runtime/printf)
          strcat(line, "\033[0m\n");  // NOLINT(runtime/printf)
        }
      }
    }

    // Overwrite the new line char
    line[strlen(line) - 1] = 0;

    // Append pid
    if (appendPid_) {
      // %s\033[33;1m(%i)\033[0m\n
      strcat(line, " ");  // NOLINT(runtime/printf)
      if (colorOutput_) {
        strcat(line, "\033[33;1m");  // NOLINT(runtime/printf)
      }
      strcat(line, "(");  // NOLINT(runtime/printf)
      strcat(line, Safe::itoa(getppid()));  // NOLINT(runtime/printf)
      strcat(line, ")");  // NOLINT(runtime/printf)
      if (colorOutput_) {
        strcat(line, "\033[0m");  // NOLINT(runtime/printf)
      }
    }

    strcat(line, "\n");  // NOLINT(runtime/printf)
    Safe::print2stderr(line);
  }

  if (threadSafe_) {
    // Resume the parent process
    kill(getppid(), SIGCONT);
  }

  // This is called in the child process
  _Exit(EXIT_SUCCESS);
}

#if __GNUC__ >= 4 && __GNUC_MINOR__ >= 6
#pragma GCC diagnostic pop
#endif

}  // namespace Debug
