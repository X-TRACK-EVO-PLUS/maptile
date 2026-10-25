# Platform
if(WIN32)
  message(STATUS "Platform: windows")
  add_definitions(-DUSE_WIN32DRV=1)
elseif(UNIX)
  message(STATUS "Platform: UNIX")

  # Warning flags - applied only to Simulator target below, not to external deps
  set(SIMULATOR_WARNING_FLAGS
      "-Wall -Wextra \
        -Wshadow -Wundef -Wmaybe-uninitialized -Wno-deprecated-declarations \
        -Wno-unused-function -Wno-error=strict-prototypes -Wpointer-arith -fno-strict-aliasing -Wno-error=cpp -Wuninitialized \
        -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-nonliteral -Wno-cast-qual -Wunreachable-code -Wno-switch-default \
        -Wreturn-type -Wmultichar -Wformat-security -Wno-ignored-qualifiers -Wno-error=pedantic -Wno-sign-compare -Wno-error=missing-prototypes -Wdouble-promotion -Wclobbered -Wdeprecated \
        -Wempty-body -Wshift-negative-value \
        -Wtype-limits -Wsizeof-pointer-memaccess -Wpointer-arith -Wno-cast-function-type -Wunused-but-set-variable"
  )

  set(SIMULATOR_WARNING_FLAGS
      "${SIMULATOR_WARNING_FLAGS} -Wno-shadow -Wno-unused-but-set-variable -Wno-double-promotion -Werror=narrowing -Wunused-function"
  )

  add_definitions(-DUSE_SDL=0 -DHAVE_UNISTD_H=1 -DHAVE_FCNTL_H=1)
  find_package(SDL2 REQUIRED SDL2)
  include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS}/../)
endif()

# Freetype
include_directories(${EXTERNAL_SOURCE_DIR}/freetype/include)
include_directories(${EXTERNAL_SOURCE_DIR}) # so "lvgl/" resolves to
                                            # external/lvgl/
add_definitions(
  -DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H=<lvgl/src/libs/freetype/ftmodule.h>
  -DFT_CONFIG_OPTIONS_H=<lvgl/src/libs/freetype/ftoption.h>)
add_subdirectory(${EXTERNAL_SOURCE_DIR}/freetype
                 ${CMAKE_CURRENT_BINARY_DIR}/build_freetype)

# PlutoVG
add_definitions(-DPLUTOVG_BUILD_STATIC)
include_directories(${EXTERNAL_SOURCE_DIR}/plutovg/include)
add_subdirectory(${EXTERNAL_SOURCE_DIR}/plutovg
                 ${CMAKE_CURRENT_BINARY_DIR}/build_plutovg)

# LVGL
include_directories(${CMAKE_CURRENT_SOURCE_DIR}) # for lv_conf.h
include_directories(${EXTERNAL_SOURCE_DIR}) # so "lvgl/" resolves to
                                            # external/lvgl/
set(LV_BUILD_CONF_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h
    CACHE PATH "" FORCE)
add_subdirectory(${EXTERNAL_SOURCE_DIR}/lvgl
                 ${CMAKE_CURRENT_BINARY_DIR}/build_lvgl)

# Propagate lvgl include paths to maptile_lvgl
target_include_directories(maptile_lvgl PUBLIC ${EXTERNAL_SOURCE_DIR}
                                               ${CMAKE_CURRENT_SOURCE_DIR})

# Executable
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
add_executable(Simulator main.cpp app_entry.cpp map_viewer.cpp)

target_include_directories(Simulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                             ${EXTERNAL_SOURCE_DIR}/..)

# Apply warning flags only to our code, not external deps
if(UNIX)
  separate_arguments(SIMULATOR_WARNING_LIST UNIX_COMMAND "${SIMULATOR_WARNING_FLAGS}")
  target_compile_options(Simulator PRIVATE ${SIMULATOR_WARNING_LIST})
endif()

# Copy resources are no longer needed - use cwd to access resource/ directly

# Link
target_link_libraries(
  Simulator
  PRIVATE stdc++
          maptile
          maptile_lvgl
          maptile_plutovg
          lvgl
          lvgl::examples
          lvgl::demos
          lvgl::thorvg
          freetype
          plutovg
          m
          ${SDL2_LIBRARIES})

set_target_properties(Simulator PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                           "${PROJECT_SOURCE_DIR}/build")
