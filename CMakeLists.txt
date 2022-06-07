cmake_minimum_required(VERSION 3.0.0)
project(DeathHandler VERSION 0.1.0)

option(DH_VERBOSE_MAKEFILE    "Enable more verbose output from Makefile builds." OFF)
option(DH_BUILD_TESTS         "Enable buld tests." OFF)
option(DH_USE_FETCH_CONTENT   "Enable use FetchContent for externals." OFF)

#########  
if(DH_VERBOSE_MAKEFILE)
	set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "ON" FORCE) 
endif()

add_library(DeathHandler death_handler.cc death_handler.h)
target_link_libraries(DeathHandler dl)

if(DH_BUILD_TESTS)
    if(DH_USE_FETCH_CONTENT)
        include(FetchContent)

        FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.11.0)
        # For Windows: Prevent overriding the parent project's compiler/linker
        # settings
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    else()
        find_package(GTest REQUIRED)
    endif()

    add_executable(death_handler_test death_handler_test.cc) 
    target_link_libraries(death_handler_test PRIVATE DeathHandler GTest::gmock_main)
endif()
