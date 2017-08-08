cmake_minimum_required (VERSION 3.0.2)
project (pistache)


OPTION(BUILD_PISTACHE_EXAMPLES "Build the pistache examples" OFF)
OPTION(BUILD_PISTACHE_TESTS "Build the unit tests" OFF)

include(CheckCXXCompilerFlag)

CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)

if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
    message(FATAL "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

include_directories (${CMAKE_CURRENT_LIST_DIR}/include)
add_subdirectory (${CMAKE_CURRENT_LIST_DIR}/src)

include_directories (${CMAKE_CURRENT_LIST_DIR}/src)

if(BUILD_PISTACHE_EXAMPLES)
add_subdirectory (${CMAKE_CURRENT_LIST_DIR}/examples)
endif(BUILD_PISTACHE_EXAMPLES)

if(BUILD_PISTACHE_TESTS)
find_package(GTest)
  if (GTEST_FOUND)
      include_directories(${GTEST_INCLUDE_DIRS})
  else()
      ADD_SUBDIRECTORY (googletest-release-1.7.0)
      include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})
  endif()
  
  enable_testing()
  add_subdirectory(tests)
endif(BUILD_PISTACHE_TESTS)
