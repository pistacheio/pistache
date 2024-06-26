# SPDX-FileCopyrightText: 2015 Mathieu Stefani
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required (VERSION 3.11)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# @Feb/2024 - CMAKE_BUILD_TYPE_INIT is not defined in cmake documentation?
# CMAKE_BUILD_TYPE is defined however
# set(CMAKE_BUILD_TYPE_INIT Release)

# Options are here, before project command, so we can use them in
# choosing whether to set CMAKE_OSX_DEPLOYMENT_TARGET
option(BUILD_SHARED_LIBS "build shared library" ON)
option(PISTACHE_BUILD_TESTS "build tests alongside the project" ON)
option(PISTACHE_ENABLE_FLAKY_TESTS "if tests are built, also run ones that are known to be flaky" OFF)
option(PISTACHE_ENABLE_NETWORK_TESTS "if tests are built, run ones needing network access" OFF)
option(PISTACHE_USE_SSL "add support for SSL server" OFF)
option(PISTACHE_PIC "Enable pistache PIC" ON) # Position-independent code lib
option(PISTACHE_BUILD_FUZZ "Build fuzzer for oss-fuzz" OFF)

string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" CMAKE_HOST_SYSTEM_NAME_LOWER)

if (CMAKE_HOST_SYSTEM_NAME_LOWER MATCHES "darwin")
  if (PISTACHE_BUILD_TESTS OR PISTACHE_ENABLE_FLAKY_TESTS OR
      PISTACHE_ENABLE_NETWORK_TESTS)
    # On macOS (Dec/2023, Sonoma 14.2.1), GTest, installed by brew, used
    # by pistache tests, requires macOS 14 or later

    # Note: Must set CMAKE_OSX_DEPLOYMENT_TARGET before project
    # command which means we can't test CMAKE_CXX_COMPILER_ID or APPLE
    # which are not set until after project command
    set(ENV{MACOSX_DEPLOYMENT_TARGET} "14.0")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
  endif()
endif()

file(READ "version.txt" VERSION_FILE_RAW)
string(STRIP "${VERSION_FILE_RAW}" VERSION_FILE)

project (pistache
    LANGUAGES C CXX
    VERSION ${VERSION_FILE})

# Must have "project" before can check for Clang/Apple/CMAKE_SYSTEM_PROCESSOR
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND APPLE AND
    CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE INTERNAL "" FORCE)
endif()

if(CMAKE_BUILD_TYPE MATCHES "Debug")
  add_compile_options(-DDEBUG=1)
endif()

# add_compile_options(-fsanitize=address)
# add_link_options(-fsanitize=address)

add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion -Wno-missing-field-initializers)

# require fat LTO objects in static library
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION OR CMAKE_CXX_FLAGS MATCHES "-flto" OR CMAKE_CXX_FLAGS MATCHES "-flto=thin")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        add_compile_options(-ffat-lto-objects)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-flto=full)
    endif()
endif()

# On Linux, libraries by default compile allowing external symbols to be
# unresolved until the time of linking an executable (i.e. of linking
# the library to the executable). On macOS by contrast we need to define
# this "-undefined dynamic_lookup" option to avoid link errors on
# linking the library when there are external dependencies
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND APPLE)
set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "${CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS} -undefined dynamic_lookup")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if (PISTACHE_USE_SSL)
    find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)
endif ()

# Set version numbers in a header file

set(VERSION_MAJOR    ${pistache_VERSION_MAJOR})
set(VERSION_MINOR    ${pistache_VERSION_MINOR})
set(VERSION_PATCH    ${pistache_VERSION_PATCH})
set(VERSION_GIT_DATE ${pistache_VERSION_TWEAK})
configure_file (
    "include/pistache/version.h.in"
    "include/pistache/version.h"
    @ONLY
)

add_subdirectory (src)

# For info on GTest::gtest GTest::gmock_main, see Pistache's
# tests/CMakeLists.txt
if (PISTACHE_BUILD_TESTS)
    include(CTest)
    find_package(GTest QUIET)
    if (NOT GTEST_FOUND)
    message("GoogleTest not found. Consider installing it on your system. Downloading it from source...")
    include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG "origin/main"
            GIT_SHALLOW true
        )
        set(BUILD_GMOCK OFF CACHE BOOL "")
        FetchContent_GetProperties(googletest)
        if(NOT googletest_POPULATED)
            FetchContent_Populate(googletest)
            add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
        endif()
    endif()
    add_subdirectory(tests)
endif()

if (PISTACHE_BUILD_FUZZ) 
    add_subdirectory(tests/fuzzers)
endif()

# format target

add_custom_target(format
    COMMAND
        ./tools/format.sh
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# CMake 3.21 defines this automatically
if (PROJECT_IS_TOP_LEVEL OR CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    install(CODE "message(FATAL_ERROR \"Please use Meson to install Pistache.
See the README for details: https://github.com/pistacheio/pistache#building-from-source\")")
endif()
