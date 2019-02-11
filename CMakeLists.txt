cmake_minimum_required (VERSION 3.1.3)
project (pistache)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra -Wno-missing-field-initializers")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -g -O0 -fprofile-arcs -ftest-coverage")
endif()


option(PISTACHE_BUILD_TESTS "build tests alongside the project" OFF)
option(PISTACHE_BUILD_EXAMPLES "build examples alongside the project" OFF)
option(PISTACHE_INSTALL "add pistache as install target (recommended)" ON)
option(PISTACHE_SSL "add support for SSL server" OFF)

find_program(CTEST_MEMORYCHECK_COMMAND NAMES valgrind)
find_program(CTEST_COVERAGE_COMMAND NAMES gcov)

INCLUDE(Dart)

add_custom_target(test_memcheck COMMAND ${CMAKE_CTEST_COMMAND}  --force-new-ctest-process --test-action memcheck)
add_custom_target(coverage COMMAND ${CMAKE_CTEST_COMMAND}  --force-new-ctest-process --test-action coverage)

# CMAKE version less than 3.8 does not understand the CMAKE_CXX_FLAGS
# set to 17, so a manual check if performed on older version
if(${CMAKE_VERSION} VERSION_LESS "3.8.0")
    include(CheckCXXCompilerFlag)
    CHECK_CXX_COMPILER_FLAG("-std=c++17" COMPILER_SUPPORTS_CXX17)
    if(COMPILER_SUPPORTS_CXX17)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
    else()
        set(CMAKE_CXX_STANDARD 14)
    endif()
else()
    # Clang exports C++17 in std::experimental namespace (tested on Clang 5 and 6).
    # This gives an error on date.h external library.
    # Following workaround forces Clang to compile at best with C++14
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_CXX_STANDARD 14)
    elseif (CMAKE_COMPILER_IS_GNUCC)
        if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0)
            set(CMAKE_CXX_STANDARD 17)
        else()
            set(CMAKE_CXX_STANDARD 14)
        endif()
    else()
        set(CMAKE_CXX_STANDARD 17)
    endif()
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (PISTACHE_SSL)
    add_definitions(-DPISTACHE_USE_SSL)
    link_libraries(-lssl -lcrypto)
endif (PISTACHE_SSL)

include_directories (${CMAKE_CURRENT_SOURCE_DIR}/include)
# Set version...

    # Major and minor version...
    set(VERSION_MAJOR 0)
    set(VERSION_MINOR 0)

    # Make available in a header file...
    configure_file (
        "include/pistache/version.h.in"
        "include/pistache/version.h"
        @ONLY
    )

# Set libraries...

    # Minimum...
    set(LIBS "-lpistache -lpthread")

    # If building with OpenSSL support...
    if(PISTACHE_SSL)
        set(LIBS "${LIBS} -lssl -lcrypto")
    endif(PISTACHE_SSL)

if(PISTACHE_INSTALL)
    # Install header...
    install (
        FILES
        ${CMAKE_BINARY_DIR}/include/pistache/version.h
        DESTINATION
        include/pistache/
    )
endif()

# Configure the pkg-config metadata...

    # Initialize the metadata variables and to support remote builds...
    set(prefix         ${CMAKE_INSTALL_PREFIX})
    set(exec_prefix    ${CMAKE_INSTALL_PREFIX}/bin)
    set(libdir         ${CMAKE_INSTALL_PREFIX}/lib)
    set(libs           ${LIBS})
    set(includedir     ${CMAKE_INSTALL_PREFIX}/include)
    set(version        ${VERSION_MAJOR}.${VERSION_MINOR})

    # Perform substitutions...
    configure_file (
        "libpistache.pc.in"
        "libpistache.pc"
        @ONLY
    )

if(PISTACHE_INSTALL)
    # Install pkg-config metadata into standard location within the prefix...
    install (
        FILES
        ${CMAKE_BINARY_DIR}/libpistache.pc
        DESTINATION
        lib/pkgconfig/
    )
endif()

add_subdirectory (src)

if (PISTACHE_BUILD_EXAMPLES)
    add_subdirectory (examples)
endif()

if (PISTACHE_BUILD_TESTS)
    find_package(GTest)
    if (GTEST_FOUND)
        include_directories(${GTEST_INCLUDE_DIRS})
    else()
        ADD_SUBDIRECTORY (googletest-release-1.7.0)
        include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})
    endif()

    enable_testing()
    add_subdirectory(tests)
endif()
