cmake_minimum_required(VERSION 3.14)
project(X-MAP)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 14)

option(ENABLE_DEBUG "Enable debug build" ON)
option(XMAP_BUILD_EXAMPLE "Build example application" ON)
option(XMAP_BUILD_TESTS "Build unit tests" OFF)
option(XMAP_WERROR "Treat warnings as errors" OFF)

if(ENABLE_DEBUG)
  message(STATUS "Build type: Debug")
  add_definitions(-DDEBUG)
  set(CMAKE_BUILD_TYPE "Debug")
else()
  message(STATUS "Build type: Release")
  set(CMAKE_BUILD_TYPE "Release")
endif()

set(EXTERNAL_SOURCE_DIR ${PROJECT_SOURCE_DIR}/external)

# Protobuf
find_package(Protobuf REQUIRED)

# cJSON
option(ENABLE_CJSON_TEST "Enable cJSON tests" OFF)
option(BUILD_SHARED_LIBS "Build shared libs" OFF)
add_subdirectory(${EXTERNAL_SOURCE_DIR}/cjson ${CMAKE_BINARY_DIR}/build_cjson)

# Core library
add_subdirectory(maptile)

# Backends (only needed for example)
if(XMAP_BUILD_EXAMPLE)
  add_subdirectory(maptile/backend/lvgl)
  add_subdirectory(maptile/backend/plutovg)
endif()

# Example
if(XMAP_BUILD_EXAMPLE)
  add_subdirectory(example)
endif()

# Tests
if(XMAP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
