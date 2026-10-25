add_executable(
  xmap_tests
  test_main.cpp
  test_tile_system.cpp
  test_map_conv.cpp
  test_tile_view.cpp
  test_routing_graph.cpp
  test_router.cpp
  test_edge_flags.cpp)

target_link_libraries(xmap_tests PRIVATE maptile)
target_include_directories(xmap_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(XMAP_WERROR)
  target_compile_options(xmap_tests PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME xmap_tests COMMAND xmap_tests
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
