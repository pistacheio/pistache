cmake_minimum_required (VERSION 3.4)

set(CMAKE_BUILD_TYPE_INIT Release)

# CMAKE Pin cxx compiler to CXX14 until update to CXX17
set(CMAKE_CXX_STANDARD 14)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project (pistache
    LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -Wconversion -pedantic -Wextra -Wno-missing-field-initializers")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/CMakeModules)

include(CheckAtomic)

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -g -O0 -ftest-coverage -fstack-protector-all --param=ssp-buffer-size=4")
endif()

option(BUILD_SHARED_LIBS "build shared library" ON)
option(PISTACHE_BUILD_TESTS "build tests alongside the project" OFF)
option(PISTACHE_ENABLE_NETWORK_TESTS "if tests are built, run ones needing network access" ON)
option(PISTACHE_BUILD_EXAMPLES "build examples alongside the project" OFF)
option(PISTACHE_BUILD_DOCS "build docs alongside the project" OFF)
option(PISTACHE_INSTALL "add pistache as install target (recommended)" ON)
option(PISTACHE_USE_SSL "add support for SSL server" OFF)

# require fat LTO objects in static library
if(CMAKE_CXX_FLAGS MATCHES "-flto")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffat-lto-objects")
endif()

if (PISTACHE_BUILD_TESTS)
    find_program(CTEST_MEMORYCHECK_COMMAND NAMES valgrind)
    find_program(CTEST_COVERAGE_COMMAND NAMES gcov)
    find_program(CMAKE_CXX_CPPCHECK NAMES cppcheck)

    if (CMAKE_CXX_CPPCHECK)
        message("-- Cppcheck found " ${CMAKE_CXX_CPPCHECK})
        list(
            APPEND CMAKE_CXX_CPPCHECK
                "--enable=all"
                "-DCPPCHECK"
                "--suppress=*:${PROJECT_SOURCE_DIR}/third-party*"
                "--suppress=*:${PROJECT_SOURCE_DIR}/tests*"
                "--suppress=noExplicitConstructor:include/pistache/optional.h"
        )
    else()
        message("-- Cppcheck not found")
        set(CMAKE_CXX_CPPCHECK "")
    endif()

    INCLUDE(Dart)

    add_custom_target(test_memcheck COMMAND ${CMAKE_CTEST_COMMAND}  --force-new-ctest-process --test-action memcheck)
    add_custom_target(coverage COMMAND ${CMAKE_CTEST_COMMAND}  --force-new-ctest-process --test-action coverage)
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

# Set public interface version...

    # Major should be incremented every time a non-backward compatible change
    #  occurs in the ABI...
    string(REGEX MATCH "SONAME_VERSION_MAJOR *([0-9]*)" _ ${VERSION_FILE})
    set(SONAME_VERSION_MAJOR ${CMAKE_MATCH_1})

    # Minor should be incremented every time a backward compatible change occurs
    #  in the ABI...
    string(REGEX MATCH "SONAME_VERSION_MINOR *([0-9]+)" _ ${VERSION_FILE})
    set(SONAME_VERSION_MINOR ${CMAKE_MATCH_1})

# Set libraries that we need to link against...

    # Minimum...
    set(LIBS "-lpistache -lpthread")
    if (NOT HAVE_CXX_ATOMICS64_WITHOUT_LIB)
        set(LIBS "-latomic")
    endif ()

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
    if (NOT GTEST_FOUND)
        ADD_SUBDIRECTORY (third-party/googletest)
        set(GTEST_INCLUDE_DIRS ${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})
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
        "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")

    # Don't include these files in the archive...
    set(CPACK_SOURCE_IGNORE_FILES
      "/build/;/debian/;/prefix/;/.git*;~$;${CPACK_SOURCE_IGNORE_FILES}")

    # Include the CPack module for generating the archive and add the standard
    #  dist target to the generated makefile...
    include(CPack)
    add_custom_target(dist COMMAND ${CMAKE_MAKE_PROGRAM} package_source)

