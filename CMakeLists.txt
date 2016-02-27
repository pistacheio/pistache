cmake_minimum_required (VERSION 2.8.7)
project (restgear)
include(CheckCXXCompilerFlag)

CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
        message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

include_directories (${CMAKE_CURRENT_SOURCE_DIR}/include)
file(GLOB HEADER_FILES "include/*.h")
install(FILES ${HEADER_FILES} DESTINATION include/rest)

add_subdirectory (thirdparty)
include_directories(${RapidJSON_SOURCE_DIR}/include)

add_subdirectory (src)
include_directories (src)

add_subdirectory (examples)

ADD_SUBDIRECTORY (googletest-release-1.7.0)
enable_testing()
include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})
add_subdirectory(tests)
