add_executable( run_mime_test mime_test.cc )
target_link_libraries(run_mime_test gtest gtest_main net)
add_test( mime_test run_mime_test )

add_executable( run_headers_test headers_test.cc )
target_link_libraries(run_headers_test gtest gtest_main net)
add_test( headers_test run_headers_test )

add_executable( run_async_test async_test.cc )
target_link_libraries(run_async_test gtest gtest_main net)
add_test( async_test run_async_test )

add_executable( run_typeid_test typeid_test.cc )
target_link_libraries(run_typeid_test gtest gtest_main)
add_test( typeid_test run_typeid_test )

add_executable( run_router_test router_test.cc )
target_link_libraries(run_router_test gtest gtest_main net)
add_test( router_test run_router_test )
