cmake_minimum_required (VERSION 3.1.3)
project (pistache)

include(GNUInstallDirs)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra -Wno-missing-field-initializers")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/CMakeModules)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -g -O0 -fprofile-arcs -ftest-coverage")
endif()

option(PISTACHE_BUILD_TESTS "build tests alongside the project" OFF)
option(PISTACHE_ENABLE_NETWORK_TESTS "if tests are built, run ones needing network access" ON)
option(PISTACHE_BUILD_EXAMPLES "build examples alongside the project" OFF)
option(PISTACHE_BUILD_DOCS "build docs alongside the project" OFF)
option(PISTACHE_INSTALL "add pistache as install target (recommended)" ON)
option(PISTACHE_USE_SSL "add support for SSL server" OFF)

if (PISTACHE_BUILD_TESTS)
    find_program(CTEST_MEMORYCHECK_COMMAND NAMES valgrind)
    find_program(CTEST_COVERAGE_COMMAND NAMES gcov)
    find_program(CMAKE_CXX_CPPCHECK NAMES cppcheck)

    if (CMAKE_CXX_CPPCHECK)
        message("-- Cppcheck found " ${CMAKE_CXX_CPPCHECK})
        list(
            APPEND CMAKE_CXX_CPPCHECK 
                "--enable=all"
                "--suppress=*:${PROJECT_SOURCE_DIR}/third-party*"
                "--suppress=*:${PROJECT_SOURCE_DIR}/tests*"
        )
    else()
        message("-- Cppcheck not found")
        set(CMAKE_CXX_CPPCHECK "")
    endif()

    INCLUDE(Dart)

    add_custom_target(test_memcheck COMMAND ${CMAKE_CTEST_COMMAND}  --force-new-ctest-process --test-action memcheck)
    add_custom_target(coverage COMMAND ${CMAKE_CTEST_COMMAND}  --force-new-ctest-process --test-action coverage)
endif()

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

if (PISTACHE_USE_SSL)
    add_definitions(-DPISTACHE_USE_SSL)
    link_libraries(-lssl -lcrypto)
endif (PISTACHE_USE_SSL)

include_directories (${CMAKE_CURRENT_SOURCE_DIR}/include)

# Set version...

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

    # Make available in a header file...
    configure_file (
        "include/pistache/version.h.in"
        "include/pistache/version.h"
        @ONLY
    )

    # Install version header...
    if(PISTACHE_INSTALL)
        install (
            FILES
            ${CMAKE_CURRENT_BINARY_DIR}/include/pistache/version.h
            DESTINATION
            ${CMAKE_INSTALL_INCLUDEDIR}/pistache/
        )
    endif()

# Set libraries...

    # Minimum...
    set(LIBS "-lpistache -lpthread")

    # If building with OpenSSL support...
    if(PISTACHE_USE_SSL)
        set(LIBS "${LIBS} -lssl -lcrypto")
    endif(PISTACHE_USE_SSL)

# Configure the pkg-config metadata...

    # Initialize the metadata variables and to support remote builds...
    set(prefix         ${CMAKE_INSTALL_PREFIX})
    set(exec_prefix    ${CMAKE_INSTALL_PREFIX}/bin)
    set(libdir         ${CMAKE_INSTALL_FULL_LIBDIR}/)
    set(libs           ${LIBS})
    set(includedir     ${CMAKE_INSTALL_FULL_INCLUDEDIR}/)
    set(version        ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}-git${VERSION_GIT_DATE})

    # Perform substitutions...
    configure_file (
        "libpistache.pc.in"
        "libpistache.pc"
        @ONLY
    )

    # Install pkg-config metadata into standard location within the prefix...
    if(PISTACHE_INSTALL)
        install (
            FILES
            ${CMAKE_CURRENT_BINARY_DIR}/libpistache.pc
            DESTINATION
            ${CMAKE_INSTALL_LIBDIR}/pkgconfig/
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
        ADD_SUBDIRECTORY (third-party/googletest)
        include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})
    endif()

    enable_testing()
    add_subdirectory(tests)
endif()

if (PISTACHE_BUILD_DOCS)

    find_package(Doxygen
            REQUIRED dot
            OPTIONAL_COMPONENTS mscgen dia)

    if (DOXYGEN_FOUND)

        set(DOXYGEN_IN ${CMAKE_SOURCE_DIR}/docs/Doxyfile.in)
        set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/docs/Doxyfile)

        configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

        add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs
            COMMENT "Generating doxygen documentation"
            VERBATIM)

        message("-- Adding doxygen documentation done")

    else(DOXYGEN_FOUND)
        message(FATAL_ERROR "Doxygen need to be installed to generate the doxygen documentation")
    endif(DOXYGEN_FOUND)

endif()

# Generate source tarball...

    # Define version of source archive...
    set(CPACK_PACKAGE_VERSION_MAJOR     ${VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR     ${VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH     ${VERSION_PATCH})

    # Generate a .tar.bz2
    set(CPACK_SOURCE_GENERATOR "TBZ2")

    # Set filename...
    set(CPACK_SOURCE_PACKAGE_FILE_NAME
        "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}-git${VERSION_GIT_DATE}")

    # Don't include these files in the archive...
    set(CPACK_SOURCE_IGNORE_FILES
      "/build/;/debian/;/prefix/;/.git*;~$;${CPACK_SOURCE_IGNORE_FILES}")

    # Include the CPack module for generating the archive and add the standard
    #  dist target to the generated makefile...
    include(CPack)
    add_custom_target(dist COMMAND ${CMAKE_MAKE_PROGRAM} package_source)

