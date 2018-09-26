cmake_minimum_required (VERSION 3.0.2)
project (pistache)
include(CheckCXXCompilerFlag)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra -Wno-missing-field-initializers")

option(PISTACHE_BUILD_TESTS "build tests alongside the project" OFF)
option(PISTACHE_BUILD_EXAMPLES "build examples alongside the project" OFF)
option(PISTACHE_INSTALL "add pistache as install target (recommended)" ON)

CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
        message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

include_directories (${CMAKE_CURRENT_SOURCE_DIR}/include)
add_subdirectory (src)

include_directories (src)

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

    # Initialize the metadata variables to support remote builds...
    set(prefix         ${CMAKE_INSTALL_PREFIX})
    set(exec_prefix    ${CMAKE_INSTALL_PREFIX}/bin)
    set(libdir         ${CMAKE_INSTALL_PREFIX}/lib)
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

