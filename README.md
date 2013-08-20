DeathHandler class installs SEGFAULT and SIGABRT signal handlers to print
a nice stack trace and (if requested) generate a core dump.
In DeathHandler's constructor, signal handlers
are installed through sigaction(). If your program encounters a segmentation
fault, the call stack is unwinded with backtrace(), converted into
function names with line numbers via addr2line (fork() + execlp()).
Addresses from shared libraries are also converted thanks to dladdr().
All C++ symbols are demangled. Printed stack trace includes the faulty
thread id obtained with pthread_self() and each line contains the process
id to distinguish several stack traces printed by different processes at
the same time.

Example
=======

test.cc:
~~~~{.cc}
#include "death_handler.h"

int main() {
  Debug::DeathHandler dh;
  int* p = NULL;
  *p = 0;
  return 0;
}
~~~~

~~~~{.sh}
g++ -g death_handler.cc test.cc -ldl -o test
./test
~~~~

This project is released under the Simplified BSD License.
Copyright 2012 Samsung R&D Institute Russia.
