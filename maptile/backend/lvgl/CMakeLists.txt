add_library(maptile_lvgl STATIC src/lvgl_vector_gfx.cpp src/lvgl_text_gfx.cpp
                                src/mvt_image_decoder.cpp)

target_include_directories(maptile_lvgl
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(maptile_lvgl PUBLIC maptile maptile_plutovg)

# lvgl must be provided by the consuming project
# target_link_libraries(maptile_lvgl PUBLIC lvgl)
