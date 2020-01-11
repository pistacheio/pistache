function(pistache_example example_name)
    set(EXAMPLE_EXECUTABLE run_${example_name})
    set(EXAMPLE_SOURCE ${example_name}.cc)

    add_executable(${EXAMPLE_EXECUTABLE} ${EXAMPLE_SOURCE})
    target_link_libraries(${EXAMPLE_EXECUTABLE} pistache_static)
endfunction()

pistache_example(hello_server)
pistache_example(http_server)
pistache_example(http_client)
pistache_example(rest_server)
pistache_example(custom_header)
pistache_example(http_server_shutdown)

set(RAPIDJSON_ROOT_DIR PATH "${PROJECT_SOURCE_DIR}/third-party/rapidjson")

find_package(RapidJSON)
if (RAPIDJSON_FOUND)
    include_directories(${RAPIDJSON_INCLUDES})
    pistache_example(rest_description)
else()
    message(FATAL_ERROR "RapidJSON not found. Could not build rest_description example.")
endif()
