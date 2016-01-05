set(SOURCE_FILES
    tcp.cc
    io.cc
    listener.cc
    net.cc
    peer.cc
    os.cc
    peer.cc
    http.cc
    http_header.cc
    http_headers.cc
    http_defs.cc
    mime.cc
    stream.cc
    router.cc
)

add_library(net ${SOURCE_FILES})
include_directories (${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(net pthread)

file(GLOB HEADER_FILES "*.h")
install(FILES ${HEADER_FILES} DESTINATION include/rest)
install(TARGETS net DESTINATION lib)
