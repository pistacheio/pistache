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

set(PISTACHE_INCLUDE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

target_include_directories(pistache PUBLIC ${PISTACHE_INCLUDE})

set(include_install_dir ${CMAKE_INSTALL_INCLUDEDIR})
set(lib_install_dir ${CMAKE_INSTALL_LIBDIR})
set(bin_install_dir ${CMAKE_INSTALL_BINDIR})

if (BUILD_SHARED_LIBS)
    add_library(pistache_shared SHARED $<TARGET_OBJECTS:pistache>)
    target_link_libraries(pistache_shared PRIVATE Threads::Threads ${CMAKE_REQUIRED_LIBRARIES})
    target_include_directories(pistache_shared INTERFACE ${PISTACHE_INCLUDE})
endif ()

add_library(pistache_static STATIC $<TARGET_OBJECTS:pistache>)
target_link_libraries(pistache_static PRIVATE Threads::Threads ${CMAKE_REQUIRED_LIBRARIES})
target_include_directories(pistache_static INTERFACE ${PISTACHE_INCLUDE})

if (PISTACHE_USE_SSL)
    target_compile_definitions(pistache PUBLIC PISTACHE_USE_SSL)
    target_compile_definitions(pistache_static PUBLIC PISTACHE_USE_SSL)

    target_include_directories(pistache PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(pistache_static PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    if (BUILD_SHARED_LIBS)
        target_compile_definitions(pistache_shared PUBLIC PISTACHE_USE_SSL)
        target_link_libraries(pistache_shared PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    endif ()
endif ()

set(Pistache_OUTPUT_NAME "pistache")
if (BUILD_SHARED_LIBS)
    set_target_properties(pistache_shared PROPERTIES
        OUTPUT_NAME ${Pistache_OUTPUT_NAME}-${version}
        SOVERSION ${SONAME_VERSION_MAJOR}.${SONAME_VERSION_MINOR}
    )
endif ()

set_target_properties(pistache_static PROPERTIES 
    OUTPUT_NAME ${Pistache_OUTPUT_NAME}
)

if (PISTACHE_INSTALL)
    set(Pistache_CMAKE_INSTALL_PATH "${CMAKE_INSTALL_LIBDIR}/cmake/pistache")
    set(Pistache_CONFIG_FILE "PistacheConfig.cmake")
    set(Pistache_CONFIG_VERSION_FILE "PistacheConfigVersion.cmake")

    if (BUILD_SHARED_LIBS)
        install(
          TARGETS pistache_shared
          EXPORT PistacheTargets
          ARCHIVE DESTINATION ${lib_install_dir}
          LIBRARY DESTINATION ${lib_install_dir}
          RUNTIME DESTINATION ${bin_install_dir}
          INCLUDES DESTINATION ${include_install_dir})
    endif ()

    install(
      DIRECTORY "${PROJECT_SOURCE_DIR}/include/pistache"
      DESTINATION ${include_install_dir}
      FILES_MATCHING PATTERN "*.*h")
    install(TARGETS pistache_static
            EXPORT PistacheTargets
            DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(EXPORT PistacheTargets
            DESTINATION ${Pistache_CMAKE_INSTALL_PATH}
            EXPORT_LINK_INTERFACE_LIBRARIES
            COMPONENT cmake-config
    )

    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        "${Pistache_CONFIG_FILE}.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${Pistache_CONFIG_FILE}"
        INSTALL_DESTINATION ${Pistache_CMAKE_INSTALL_PATH}
        PATH_VARS include_install_dir lib_install_dir
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${Pistache_CONFIG_VERSION_FILE}"
        VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
        COMPATIBILITY AnyNewerVersion)
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/${Pistache_CONFIG_FILE}"
        "${CMAKE_CURRENT_BINARY_DIR}/${Pistache_CONFIG_VERSION_FILE}"
        DESTINATION ${Pistache_CMAKE_INSTALL_PATH}
        COMPONENT cmake-config
    )
endif()
