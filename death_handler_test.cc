/*! @file death_handler.cc
 *  @brief Tests for DeathHandler.
 *  @author Markovtsev Vadim <v.markovtsev@samsung.com>
 *  @version 1.0
 *
 *  @section Notes
 *  This code partially conforms to <a href="http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml">Google C++ Style Guide</a>.
 *
 *  @section Copyright
 *  Copyright 2012 Samsung R&D Institute Russia
 */

#include "death_handler.h"
#include <malloc.h>
#include <gtest/gtest.h>
#include <thread>  // NOLINT(build/include_order)

using Debug::DeathHandler;

#define SEGMENTATION_FAULT() do { \
                               int *p = 0; \
                               *p = 0; \
                             } while (false)

TEST(DeathHandler, Simple) {
  int pipefd[2];
  assert(pipe(pipefd) == 0);

  int pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    DeathHandler dh;
    SEGMENTATION_FAULT();
  }
  close(pipefd[1]);
  wait(nullptr);
  char text[4096];
  int bytesRead;
  int totalBytesRead = 0;
  while ((bytesRead = read(pipefd[0], &text[totalBytesRead],
                          sizeof(text) - totalBytesRead)) > 0) {
    totalBytesRead += bytesRead;
  }
  close(pipefd[0]);
  auto posstr = strstr(text, "Segmentation fault");
  ASSERT_NE(static_cast<char*>(nullptr), posstr);
  posstr = strstr(text, "[DeathHandler_Simple_Test::TestBody()]");
  ASSERT_NE(static_cast<char*>(nullptr), posstr);
  posstr = strstr(text, "death_handler.cc");
  ASSERT_NE(static_cast<char*>(nullptr), posstr);
  // Warning: hard-coded line number
  posstr = strstr(text, ":36");
  ASSERT_NE(static_cast<char*>(nullptr), posstr);
}

void *malloc_hook(size_t, const void*) {
  __malloc_hook = nullptr;
  __free_hook = nullptr;
  fprintf(stderr, "malloc() call detected\n");
  _Exit(EXIT_FAILURE);
}

void free_hook(void*, const void*) {
  __malloc_hook = nullptr;
  __free_hook = nullptr;
  fprintf(stderr, "free() call detected\n");
  _Exit(EXIT_FAILURE);
}

void cancel_malloc_hook() {
  __malloc_hook = nullptr;
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

#include "tests/google/src/gtest_main.cc"
