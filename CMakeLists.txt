cmake_minimum_required (VERSION 2.8.7)
project (restgear)

if(CMAKE_COMPILER_IS_GNUCXX)
    add_definitions(-std=c++0x)
endif()

add_subdirectory (src)
include_directories (src)

add_executable(server main.cc)
target_link_libraries(server net)

ADD_SUBDIRECTORY (googletest-release-1.7.0)
enable_testing()
include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})
add_subdirectory(tests)
