function(pistache_test test_name)
    set(TEST_EXECUTABLE run_${test_name})
    set(TEST_SOURCE ${test_name}.cc)

    add_executable(${TEST_EXECUTABLE} ${TEST_SOURCE})
    target_link_libraries(${TEST_EXECUTABLE} gtest gtest_main pistache)
    add_test(${test_name} ${TEST_EXECUTABLE})
endfunction()


pistache_test(mime_test)
pistache_test(headers_test)
pistache_test(async_test)
pistache_test(typeid_test)
pistache_test(router_test)
pistache_test(cookie_test)
pistache_test(view_test)
pistache_test(http_parsing_test)
