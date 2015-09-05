set(SOURCE_FILES
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
)

add_library(net ${SOURCE_FILES})
include_directories (${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(net pthread)
