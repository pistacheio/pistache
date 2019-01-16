file (GLOB COMMON_SOURCE_FILES "common/*.cc")
file (GLOB SERVER_SOURCE_FILES "server/*.cc")
file (GLOB CLIENT_SOURCE_FILES "client/*.cc")

file (GLOB INCLUDE_FILES ${PROJECT_SOURCE_DIR}/include/pistache/*h)

set(SOURCE_FILES
    ${COMMON_SOURCE_FILES}
    ${SERVER_SOURCE_FILES}
    ${CLIENT_SOURCE_FILES}
    ${INCLUDE_FILES}
)

add_library(pistache OBJECT ${SOURCE_FILES})
set_target_properties(pistache PROPERTIES POSITION_INDEPENDENT_CODE 1)
add_definitions(-DONLY_C_LOCALE=1)

target_include_directories(pistache PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
)

set(include_install_dir "include")
set(lib_install_dir "lib/")
set(bin_install_dir "bin/")

add_library(pistache_shared SHARED $<TARGET_OBJECTS:pistache>)
add_library(pistache_static STATIC $<TARGET_OBJECTS:pistache>)

target_link_libraries(pistache_shared pthread)
target_link_libraries(pistache_static pthread)

set_target_properties(pistache_shared PROPERTIES
    OUTPUT_NAME "pistache"
    VERSION ${version}
    SOVERSION ${VERSION_MAJOR}
)

set_target_properties(pistache_static PROPERTIES OUTPUT_NAME "pistache")

if (PISTACHE_INSTALL)
    install(
      TARGETS pistache_shared
      EXPORT ${targets_export_name}
      ARCHIVE DESTINATION ${lib_install_dir}
      LIBRARY DESTINATION ${lib_install_dir}
      RUNTIME DESTINATION ${bin_install_dir}
      INCLUDES DESTINATION ${include_install_dir})

    install(
      DIRECTORY "${PROJECT_SOURCE_DIR}/include/pistache"
      DESTINATION ${include_install_dir}
      FILES_MATCHING PATTERN "*.*h")
    install(TARGETS pistache_static
            EXPORT PistacheTargets
            DESTINATION lib)
    install(EXPORT PistacheTargets
            DESTINATION "lib/cmake/pistache"
            EXPORT_LINK_INTERFACE_LIBRARIES
            COMPONENT cmake-config
    )

    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        "PistacheConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/PistacheConfig.cmake"
        INSTALL_DESTINATION "lib/cmake/pistache"
    )
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/PistacheConfig.cmake"
        DESTINATION "lib/cmake/pistache"
        COMPONENT cmake-config
    )
endif()
