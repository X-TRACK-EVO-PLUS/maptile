cmake_minimum_required(VERSION 3.14)
project(
  maptile
  VERSION 0.1.0
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Only the core library is built when maptile is used as a subproject
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(MAPTILE_TOP_LEVEL ON)
else()
  set(MAPTILE_TOP_LEVEL OFF)
endif()

option(MAPTILE_WITH_ZLIB "Read gzip-compressed tiles (needs zlib)" ON)
option(MAPTILE_WITH_SQLITE "Read MBTiles files (needs sqlite3)" ON)
option(MAPTILE_BUILD_SVG "Build the SVG backend and the maptile-svg tool"
       ${MAPTILE_TOP_LEVEL})
option(MAPTILE_BUILD_TESTS "Build the unit tests" ${MAPTILE_TOP_LEVEL})
option(MAPTILE_WERROR "Treat warnings as errors" OFF)

add_library(
  maptile STATIC
  src/camera.cpp
  src/geom.cpp
  src/json.cpp
  src/mvt.cpp
  src/renderer.cpp
  src/style.cpp
  src/tile_source.cpp
  src/gcj02.c
  src/routing_graph.cpp
  src/router.cpp)
add_library(maptile::maptile ALIAS maptile)
target_include_directories(
  maptile PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_options(maptile PRIVATE -Wall -Wextra
                                       $<$<BOOL:${MAPTILE_WERROR}>:-Werror>)
set_target_properties(maptile PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MAPTILE_WITH_ZLIB)
  find_package(ZLIB)
  if(ZLIB_FOUND)
    target_link_libraries(maptile PRIVATE ZLIB::ZLIB)
    target_compile_definitions(maptile PRIVATE MAPTILE_WITH_ZLIB=1)
  else()
    message(STATUS "maptile: zlib not found, gzip tiles are not supported")
  endif()
endif()
if(MAPTILE_WITH_SQLITE)
  find_package(SQLite3)
  if(SQLite3_FOUND)
    target_link_libraries(maptile PRIVATE SQLite::SQLite3)
    target_compile_definitions(maptile PRIVATE MAPTILE_WITH_SQLITE=1)
  else()
    message(STATUS "maptile: sqlite3 not found, MBTiles are not supported")
  endif()
endif()

if(MAPTILE_BUILD_SVG)
  add_library(maptile_svg STATIC backends/svg/svg_canvas.cpp)
  target_include_directories(maptile_svg
                             PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backends/svg)
  target_link_libraries(maptile_svg PUBLIC maptile)
  add_executable(maptile-svg tools/maptile_svg.cpp)
  target_link_libraries(maptile-svg PRIVATE maptile_svg)
endif()

if(MAPTILE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
