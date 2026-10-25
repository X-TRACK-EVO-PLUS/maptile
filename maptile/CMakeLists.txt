protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
                      ${CMAKE_CURRENT_SOURCE_DIR}/proto/vector_tile.proto)

add_library(
  maptile STATIC
  src/def_style.cpp
  src/json_style.cpp
  src/tile_utils.cpp
  src/tile_parser.cpp
  src/tile_renderer.cpp
  src/stroke_to_path.cpp
  src/mbtiles_reader.cpp
  src/tile_view.cpp
  src/map_conv.cpp
  src/tile_system.cpp
  src/gcj02.c
  src/routing_graph.cpp
  src/router.cpp
  ${PROTO_SRCS}
  ${PROTO_HDRS})

target_include_directories(
  maptile
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
         ${CMAKE_CURRENT_BINARY_DIR} # for generated .pb.h
         ${PROTOBUF_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/../external/cjson)

target_include_directories(maptile PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(maptile PUBLIC ${Protobuf_LIBRARIES} cjson)

find_package(ZLIB REQUIRED)
target_link_libraries(maptile PRIVATE ZLIB::ZLIB)

find_library(SQLITE3_LIBRARY sqlite3)
target_link_libraries(maptile PRIVATE ${SQLITE3_LIBRARY})

if(XMAP_WERROR)
  target_compile_options(maptile PRIVATE -Wall -Wextra -Werror)
endif()
