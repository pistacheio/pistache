add_executable(http_server http_server.cc)
target_link_libraries(http_server net_static)

add_executable(hello_server hello_server.cc)
target_link_libraries(hello_server net_static)

add_executable(http_client http_client.cc)
target_link_libraries(http_client net_static)

add_executable(custom_header custom_header.cc)
target_link_libraries(custom_header net_static)

add_executable(rest_server rest_server.cc)
target_link_libraries(rest_server net_static)

find_package(RapidJSON)
if (RapidJSON_FOUND)
include_directories(${RapidJSON_INCLUDE_DIRS})
add_executable(rest_description rest_description.cc)
target_link_libraries(rest_description net_static)
endif()
