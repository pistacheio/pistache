add_executable( run_mime_test mime_test.cc )
target_link_libraries(run_mime_test gtest gtest_main net)
add_test( mime_test run_mime_test )
