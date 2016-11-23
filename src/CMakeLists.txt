file (GLOB COMMON_SOURCE_FILES "common/*.cc")
file (GLOB SERVER_SOURCE_FILES "server/*.cc")
file (GLOB CLIENT_SOURCE_FILES "client/*.cc")

set(SOURCE_FILES
    ${COMMON_SOURCE_FILES}
    ${SERVER_SOURCE_FILES}
    ${CLIENT_SOURCE_FILES}
)

add_library(pistache_static ${SOURCE_FILES})
target_link_libraries(pistache_static pthread)
install(TARGETS pistache_static DESTINATION lib)

add_library(pistache SHARED ${SOURCE_FILES})
target_link_libraries(pistache pthread)
install(TARGETS pistache DESTINATION lib)

