file (GLOB COMMON_SOURCE_FILES "common/*.cc")
file (GLOB SERVER_SOURCE_FILES "server/*.cc")
file (GLOB CLIENT_SOURCE_FILES "client/*.cc")

set(SOURCE_FILES
    ${COMMON_SOURCE_FILES}
    ${SERVER_SOURCE_FILES}
    ${CLIENT_SOURCE_FILES}
)

add_library(net_static ${SOURCE_FILES})
target_link_libraries(net_static pthread)
install(TARGETS net_static DESTINATION lib)

add_library(net SHARED ${SOURCE_FILES})
target_link_libraries(net pthread)
install(TARGETS net DESTINATION lib)

