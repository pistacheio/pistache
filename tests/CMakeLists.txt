add_executable( run_mime_test mime_test.cc )
target_link_libraries(run_mime_test gtest gtest_main net)
add_test( mime_test run_mime_test )

add_executable( run_headers_test headers_test.cc )
target_link_libraries(run_headers_test gtest gtest_main net)
add_test( headers_test run_headers_test )

add_executable( run_async_test async_test.cc )
target_link_libraries(run_async_test gtest gtest_main net)
add_test( async_test run_async_test )
