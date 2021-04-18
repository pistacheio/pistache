cmake_minimum_required (VERSION 3.11)

set(CMAKE_BUILD_TYPE_INIT Release)

# CMAKE Pin cxx compiler to CXX17
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project (pistache
    LANGUAGES C CXX)

include(GNUInstallDirs)

add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion -Wno-missing-field-initializers)

option(BUILD_SHARED_LIBS "build shared library" ON)
option(PISTACHE_BUILD_TESTS "build tests alongside the project" OFF)
option(PISTACHE_ENABLE_NETWORK_TESTS "if tests are built, run ones needing network access" ON)
option(PISTACHE_USE_SSL "add support for SSL server" OFF)
option(PISTACHE_PIC "Enable pistache PIC" ON)

# require fat LTO objects in static library
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION OR CMAKE_CXX_FLAGS MATCHES "-flto" OR CMAKE_CXX_FLAGS MATCHES "-flto=thin")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        add_compile_options(-ffat-lto-objects)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-flto=full)
    endif()
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if (PISTACHE_USE_SSL)
    find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)
endif ()

# Set release version...

    # Retrieve from external file...
    file(READ "version.txt" VERSION_FILE)

    # Extract version...

        # Major...
        string(REGEX MATCH "VERSION_MAJOR *([0-9]*)" _ ${VERSION_FILE})
        set(VERSION_MAJOR ${CMAKE_MATCH_1})

        # Minor...
        string(REGEX MATCH "VERSION_MINOR *([0-9]+)" _ ${VERSION_FILE})
        set(VERSION_MINOR ${CMAKE_MATCH_1})

        # Patch...
        string(REGEX MATCH "VERSION_PATCH *([0-9]+)" _ ${VERSION_FILE})
        set(VERSION_PATCH ${CMAKE_MATCH_1})

        # Git date...
        string(REGEX MATCH "VERSION_GIT_DATE *([0-9]+)" _ ${VERSION_FILE})
        set(VERSION_GIT_DATE ${CMAKE_MATCH_1})

        # Full version
        set(VERSION        ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}-git${VERSION_GIT_DATE})

    # Make available in a header file...
    configure_file (
        "include/pistache/version.h.in"
        "include/pistache/version.h"
        @ONLY
    )

# Set public interface version...

    # Major should be incremented every time a non-backward compatible change
    #  occurs in the ABI...
    string(REGEX MATCH "SONAME_VERSION_MAJOR *([0-9]*)" _ ${VERSION_FILE})
    set(SONAME_VERSION_MAJOR ${CMAKE_MATCH_1})

    # Minor should be incremented every time a backward compatible change occurs
    #  in the ABI...
    string(REGEX MATCH "SONAME_VERSION_MINOR *([0-9]+)" _ ${VERSION_FILE})
    set(SONAME_VERSION_MINOR ${CMAKE_MATCH_1})

add_subdirectory (src)

if (PISTACHE_BUILD_TESTS)
    include(CTest)
    find_package(GTest QUIET)
    if (NOT GTEST_FOUND)
    message("GoogleTest not found. Consider installing it on your system. Downloading it from source...")
    include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
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

# format target

add_custom_target(format
    COMMAND
        ./tools/format.sh
    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}
)
