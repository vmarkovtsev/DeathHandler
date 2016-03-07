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

 */

/*! @file death_handler.cc
 *  @brief Tests for DeathHandler.
 *  @author Markovtsev Vadim <v.markovtsev@samsung.com>
 *  @version 1.0
 *
 *  @section Notes
 *  This code partially conforms to <a href="http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml">Google C++ Style Guide</a>.
 *
 *  @license Simplified BSD License
 *  @copyright 2012 Samsung R&D Institute Russia
 */

#include "death_handler.h"
#include <malloc.h>
#include <gtest/gtest.h>
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

using Debug::DeathHandler;

#define SEGMENTATION_FAULT() do { \
                               int *p = 0; \
                               *p = 0; \
                             } while (false)

TEST(DeathHandler, SimpleSIGSEGV) {
  int pipefd[2];
  assert(pipe(pipefd) == 0);

  int lineno = __LINE__;
  int pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    DeathHandler dh;
    SEGMENTATION_FAULT();
  }
  close(pipefd[1]);
  wait(NULL);
  char text[4096];
  int bytesRead;
  int totalBytesRead = 0;
  while ((bytesRead = read(pipefd[0], &text[totalBytesRead],
                           sizeof(text) - totalBytesRead)) > 0) {
    totalBytesRead += bytesRead;
  }
  close(pipefd[0]);
  printf("%s", text);
  char* posstr = strstr(text, "Segmentation fault");
  ASSERT_NE(static_cast<const char*>(NULL), posstr);
  posstr = strstr(text, "[DeathHandler_SimpleSIGSEGV_Test::TestBody()]");
  ASSERT_NE(static_cast<const char*>(NULL), posstr);
  posstr = strstr(text, "death_handler");
  ASSERT_NE(static_cast<const char*>(NULL), posstr);
  // Warning: hard-coded line number
  posstr = strstr(text, ":");
  ASSERT_NE(static_cast<const char*>(NULL), posstr);
  int rlineno = atoi(posstr + 1);
  ASSERT_LT(rlineno - lineno - 7, 2);
}

TEST(DeathHandler, SimpleSIGSEGVWithoutColors) {
  int pipefd[2];
  assert(pipe(pipefd) == 0);

  int lineno = __LINE__;
  int pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    DeathHandler dh;
    dh.set_color_output(false);
    SEGMENTATION_FAULT();
  }
  close(pipefd[1]);
  wait(NULL);
  char text[4096];
  int bytesRead;
  int totalBytesRead = 0;
  while ((bytesRead = read(pipefd[0], &text[totalBytesRead],
                           sizeof(text) - totalBytesRead)) > 0) {
    totalBytesRead += bytesRead;
  }
  close(pipefd[0]);
  printf("%s", text);
  char* posstr = strstr(text, "Segmentation fault");
  ASSERT_NE(static_cast<const char*>(NULL), posstr);
  posstr = strstr(
      text, "[DeathHandler_SimpleSIGSEGVWithoutColors_Test::TestBody()]");
  ASSERT_NE(static_cast<const char*>(NULL), posstr);
  posstr = strstr(text, "death_handler");
  ASSERT_NE(static_cast<const char*>(NULL), posstr);
  // Warning: hard-coded line number
  posstr = strstr(text, ":");
  ASSERT_NE(static_cast<const char*>(NULL), posstr);
  int glineno = atoi(posstr + 1);
  ASSERT_LT(glineno - lineno - 7, 2);
}

TEST(DeathHandler, SimpleSIGABRT) {
  int pipefd[2];
  assert(pipe(pipefd) == 0);

  int pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    DeathHandler dh;
    assert(false);
  }
  close(pipefd[1]);
  wait(NULL);
  char text[4096];
  int bytesRead;
  int totalBytesRead = 0;
  while ((bytesRead = read(pipefd[0], &text[totalBytesRead],
                          sizeof(text) - totalBytesRead)) > 0) {
    totalBytesRead += bytesRead;
  }
  close(pipefd[0]);
  printf("%s", text);
  char* posstr = strstr(text, "Aborted");
  ASSERT_NE(static_cast<const char*>(NULL), posstr);
  posstr = strstr(text, "[DeathHandler_SimpleSIGABRT_Test::TestBody()]");
  ASSERT_NE(static_cast<const char*>(NULL), posstr);
  posstr = strstr(text, "death_handler");
  ASSERT_NE(static_cast<const char*>(NULL), posstr);
  // Warning: hard-coded line number
  posstr = strstr(text, ":38");
  ASSERT_NE(static_cast<const char*>(NULL), posstr);
}

void *malloc_hook(size_t, const void*) {
  __malloc_hook = NULL;
  __free_hook = NULL;
  fprintf(stderr, "malloc() call detected\n");
  _Exit(EXIT_FAILURE);
}

void free_hook(void*, const void*) {
  __malloc_hook = NULL;
  __free_hook = NULL;
  fprintf(stderr, "free() call detected\n");
  _Exit(EXIT_FAILURE);
}

void cancel_malloc_hook() {
  __malloc_hook = NULL;
}

void *memalign_hook(size_t, size_t, const void*) {
  fprintf(stderr, "memalign() call detected\n");
  _Exit(EXIT_FAILURE);
}

void *realloc_hook(void*, size_t, const void*) {
  fprintf(stderr, "realloc() call detected\n");
  _Exit(EXIT_FAILURE);
}

TEST(DeathHandler, malloc) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  EXPECT_DEATH({
    DeathHandler dh;
    atexit(cancel_malloc_hook);
    __malloc_hook = malloc_hook;
    __realloc_hook = realloc_hook;
    __memalign_hook = memalign_hook;
    __free_hook = free_hook;
    SEGMENTATION_FAULT();
  }, ".*testing::UnitTest::Run\\(\\).*");
}
/*
TEST(DeathHandler, Threading) {
  EXPECT_DEATH({
    DeathHandler dh;
    dh.set_appendPid(true);
    std::thread([]() {
      while (true) {
        printf("Running thread! %i\n", getpid());
      }
    }).detach();
    SEGMENTATION_FAULT();
  }, ".*testing::UnitTest::Run\\(\\).*");
}*/

#include "src/gtest_main.cc"
