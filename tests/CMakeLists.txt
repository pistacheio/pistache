# SPDX-FileCopyrightText: 2015 Mathieu Stefani
#
# SPDX-License-Identifier: Apache-2.0

find_package(CURL REQUIRED)

add_subdirectory(helpers)

# Enable access to lib and inc files in our local pistache branch
# Note: PROJECT_BINARY_DIR is full path to build dir of the most
# recent project() command.
include_directories(BEFORE ${PROJECT_BINARY_DIR}/../include)
link_directories(BEFORE ${PROJECT_BINARY_DIR}/src)

# Enable access to brew-installed lib and inc files, including gtest
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND APPLE)
  include_directories(AFTER ${HOMEBREW_PREFIX}/include)
  link_directories(AFTER ${HOMEBREW_PREFIX}/lib)
endif()

function(pistache_test test_name)
    set(TEST_EXECUTABLE run_${test_name})
    set(TEST_SOURCE ${test_name}.cc)

    add_executable(${TEST_EXECUTABLE} ${TEST_SOURCE})

    # On macOS (Dec/2023, Sonoma 14.2.1), GTest installed by brew
    # requires C++14 or later
    # Also, pistache header files use nested namespace definitions,
    # which is a C++17 extension per macOS error message
    # This target_compile_features might be a good idea on Linux too?
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND APPLE)           
      target_compile_features(${TEST_EXECUTABLE} PRIVATE cxx_std_17)
    endif()

    target_include_directories(${TEST_EXECUTABLE} PRIVATE
        ${CURL_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/subprojects/hinnant-date/include
        ${CMAKE_SOURCE_DIR}/subprojects/cpp-httplib
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND APPLE)
      # Apparently need gmock gtest AND gtest_main
      # https://github.com/google/googletest/issues/1404
      if (PISTACHE_USE_SSL)
        target_link_libraries(${TEST_EXECUTABLE} gmock gtest gtest_main
          ssl crypto
          pistache tests_helpers event event_pthreads ${CURL_LIBRARIES})
      else ()
        target_link_libraries(${TEST_EXECUTABLE} gmock gtest gtest_main
          pistache tests_helpers event event_pthreads ${CURL_LIBRARIES})
      endif (PISTACHE_USE_SSL)

    else ()
      # NOT macOS
      # CMake 3.20 and upstream GTest define GTest::gtest, older versions define GTest::GTest
      
      # On Ubuntu 22.04 (Jan 2024), got GTest::gtest GTest::gmock_main
      # correctly defined by installing as per this gist's
      # install_gtest_gmock.sh:
      #   https://gist.github.com/dlime/313f74fd23e4267c4a915086b84c7d3d
      #
      # Can remove "event event_pthreads" below if not forcing 
      if (TARGET GTest::gtest)
        target_link_libraries(${TEST_EXECUTABLE} GTest::gtest GTest::gmock_main
          pistache_static tests_helpers event event_pthreads ${CURL_LIBRARIES})
      else ()
        target_link_libraries(${TEST_EXECUTABLE} GTest::GTest GMock::Main pistache_static tests_helpers event event_pthreads ${CURL_LIBRARIES})
      endif ()
    endif()

    add_test(${test_name} ${TEST_EXECUTABLE})
endfunction()


pistache_test(mime_test)
pistache_test(headers_test)
pistache_test(async_test)
pistache_test(typeid_test)
pistache_test(router_test)
pistache_test(cookie_test)
pistache_test(cookie_test_2)
pistache_test(cookie_test_3)
pistache_test(view_test)
pistache_test(http_parsing_test)
pistache_test(http_uri_test)
pistache_test(http_server_test)
pistache_test(http_client_test)
if (PISTACHE_ENABLE_NETWORK_TESTS)
    pistache_test(net_test)
endif (PISTACHE_ENABLE_NETWORK_TESTS)
pistache_test(listener_test)
pistache_test(request_size_test)
pistache_test(streaming_test)
pistache_test(rest_server_test)
pistache_test(mailbox_test)
pistache_test(stream_test)
pistache_test(reactor_test)
pistache_test(threadname_test)
pistache_test(log_api_test)
pistache_test(string_logger_test)
pistache_test(endpoint_initialization_test)
pistache_test(helpers_test)

if (PISTACHE_USE_SSL)

    configure_file("certs/server.crt" "certs/server.crt" COPYONLY)
    configure_file("certs/server.key" "certs/server.key" COPYONLY)
    configure_file("certs/client.key" "certs/client.key" COPYONLY)
    configure_file("certs/client.crt" "certs/client.crt" COPYONLY)
    configure_file("certs/rootCA.crt" "certs/rootCA.crt" COPYONLY)
    configure_file("certs/server_from_intermediate_with_chain.crt" "certs/server_from_intermediate_with_chain.crt" COPYONLY)
    configure_file("certs/server_from_intermediate.crt" "certs/server_from_intermediate.crt" COPYONLY)
    configure_file("certs/server_from_intermediate.key" "certs/server_from_intermediate.key" COPYONLY)
    configure_file("certs/intermediateCA.crt" "certs/intermediateCA.crt" COPYONLY)
    configure_file("certs/server_protected.crt" "certs/server_protected.crt" COPYONLY)
    configure_file("certs/server_protected.key" "certs/server_protected.key" COPYONLY)

    pistache_test(https_server_test)
endif (PISTACHE_USE_SSL)
