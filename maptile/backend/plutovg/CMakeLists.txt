add_library(maptile_plutovg STATIC src/plutovg_gfx.cpp src/plutovg_text_gfx.cpp)

target_include_directories(maptile_plutovg
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(maptile_plutovg PUBLIC maptile plutovg)
