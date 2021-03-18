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

find_package(RapidJSON QUIET)
if (NOT RapidJSON_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        RapidJSON
        GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
        GIT_TAG v1.1.0
        GIT_SHALLOW true
    )
    set(RAPIDJSON_BUILD_DOC OFF CACHE BOOL "")
    set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "")
    set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL "")
    if(NOT RapidJSON_POPULATED)
        FetchContent_Populate(RapidJSON)
        add_subdirectory(${rapidjson_SOURCE_DIR} ${rapidjson_BINARY_DIR})
    endif()
    pistache_example(rest_description)
    target_include_directories(run_rest_description PRIVATE ${rapidjson_SOURCE_DIR}/include)
else()
    pistache_example(rest_description)
    target_include_directories(run_rest_description PRIVATE ${RAPIDJSON_INCLUDE_DIRS})
endif()
