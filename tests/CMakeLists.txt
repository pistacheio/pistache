add_executable( run_mime_test mime_test.cc )
target_link_libraries(run_mime_test gtest gtest_main net_static)
add_test( mime_test run_mime_test )

add_executable( run_headers_test headers_test.cc )
target_link_libraries(run_headers_test gtest gtest_main net_static)
add_test( headers_test run_headers_test )

add_executable( run_async_test async_test.cc )
target_link_libraries(run_async_test gtest gtest_main net_static)
add_test( async_test run_async_test )

add_executable( run_typeid_test typeid_test.cc )
target_link_libraries(run_typeid_test gtest gtest_main pthread)
add_test( typeid_test run_typeid_test )

add_executable( run_router_test router_test.cc )
target_link_libraries(run_router_test gtest gtest_main net_static)
add_test( router_test run_router_test )

add_executable( run_cookie_test cookie_test.cc )
target_link_libraries(run_cookie_test gtest gtest_main net_static)
add_test( cookie_test run_cookie_test )

add_executable( run_view_test view_test.cc )
target_link_libraries(run_view_test gtest gtest_main net_static)
add_test( view_test run_view_test )
