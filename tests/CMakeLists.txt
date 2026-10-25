add_executable(
  maptile_tests
  test_main.cpp
  test_camera.cpp
  test_edge_flags.cpp
  test_mvt.cpp
  test_renderer.cpp
  test_router.cpp
  test_routing_graph.cpp
  test_style.cpp)
target_link_libraries(maptile_tests PRIVATE maptile_svg)
target_include_directories(maptile_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(
  maptile_tests PRIVATE -Wall -Wextra $<$<BOOL:${MAPTILE_WERROR}>:-Werror>)
add_test(
  NAME maptile_tests
  COMMAND maptile_tests
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
