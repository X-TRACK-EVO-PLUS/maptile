# 仓库目录结构重构方案

## 1. 当前问题

```
X-MAP/
├── MapTile/              # 核心库（命名不够通用）
├── app_entry.cpp         # 示例应用（散落在根目录）
├── main.cpp              # SDL 模拟器入口（散落在根目录）
├── lv_conf.h             # LVGL 配置（散落在根目录）
├── CMakeLists.txt        # 顶层构建（库和示例混在一起）
├── lvgl/                 # submodule（根目录级别）
├── external/             # 三方库
├── resource/             # 示例资源
├── py_mvt_render/        # Python 工具（和 C++ 库混在一起）
├── tools/                # 开发工具
├── docs/                 # 文档
├── style.json            # 散落的配置文件
└── test.mvt              # 散落的测试文件
```

问题：
- 核心库代码和示例应用代码混在同一层级，无法被其他项目直接 `add_subdirectory`
- `lvgl` submodule 在根目录，嵌入其他项目时会和宿主项目的 lvgl 冲突
- `lv_conf.h` 在根目录，这是示例应用的配置，不属于库
- `main.cpp` / `app_entry.cpp` 是模拟器代码，不属于库
- `MapTile` 命名太具体，实际上已经是一个通用的矢量瓦片渲染库

## 2. 设计目标

1. 核心库可以被其他项目通过 `add_subdirectory` 或 submodule 直接引入
2. 核心库不依赖 lvgl（lvgl 后端是可选的）
3. 示例应用和核心库完全分离
4. 三方依赖（protobuf、plutovg）由使用方管理，库只声明依赖
5. 目录命名清晰，职责单一

## 3. 目标结构

```
X-MAP/
├── maptile/                    # 核心库 + 后端（整体作为 submodule 引入）
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── maptile/
│   │       ├── types.h
│   │       ├── style.h
│   │       ├── map_style.h
│   │       ├── tile_parser.h
│   │       ├── tile_renderer.h
│   │       ├── stroke_to_path.h
│   │       └── gfx/
│   │           ├── vector_gfx.h
│   │           └── text_gfx.h
│   ├── src/
│   │   ├── map_style.cpp
│   │   ├── tile_utils.cpp
│   │   ├── tile_utils.h
│   │   ├── tile_parser.cpp
│   │   ├── tile_renderer.cpp
│   │   └── stroke_to_path.cpp
│   ├── proto/
│   │   └── vector_tile.proto
│   │
│   └── backend/
│       ├── lvgl/
│       │   ├── CMakeLists.txt
│       │   ├── include/
│       │   │   └── maptile_lvgl/
│       │   │       ├── lvgl_vector_gfx.h
│       │   │       └── lvgl_text_gfx.h
│       │   └── src/
│       │       ├── lvgl_vector_gfx.cpp
│       │       └── lvgl_text_gfx.cpp
│       │
│       └── plutovg/
│           ├── CMakeLists.txt
│           ├── include/
│           │   └── maptile_plutovg/
│           │       ├── plutovg_gfx.h
│           │       └── plutovg_text_gfx.h
│           └── src/
│               ├── plutovg_gfx.cpp
│               └── plutovg_text_gfx.cpp
│
├── test/
│   ├── CMakeLists.txt
│   ├── test_stroke_to_path.cpp
│   ├── test_tile_parser.cpp
│   └── test_map_style.cpp
│
├── example/
│   ├── CMakeLists.txt
│   ├── app_entry.cpp
│   ├── main.cpp
│   ├── lv_conf.h
│   └── resource/
│       ├── *.mvt
│       ├── *.ttf
│       └── style.json
│
├── external/
│   ├── freetype/
│   ├── lvgl/
│   ├── plutovg/
│   ├── protobuf/
│   └── vector-tile-spec/
│
├── tools/
│   ├── code_format.sh
│   └── py_mvt_render/
│
├── docs/
├── .clang-format
├── .gitignore
├── .gitmodules
├── CMakeLists.txt
├── LICENSE
└── README.md
```

## 4. 各模块职责

### 4.1 maptile（核心库）

职责：矢量瓦片解析 + 渲染调度 + stroke-to-path + 样式系统

依赖：
- protobuf（用于 .proto 编译和 Tile 解析）
- C++ 标准库

不依赖：
- lvgl（任何 LVGL 头文件）
- plutovg
- freetype
- SDL

对外暴露的头文件通过 `include/maptile/` 目录提供，使用方 include 路径为：
```cpp
#include <maptile/TileParser.h>
#include <maptile/TileRenderer.h>
#include <maptile/gfx/VectorGfx.h>
```

CMake 使用方式：
```cmake
add_subdirectory(path/to/maptile)
target_link_libraries(my_app PRIVATE maptile)
```

### 4.2 maptile/backend/lvgl（LVGL 后端）

职责：实现 `VectorGfx` 和 `TextGfx` 的 LVGL 版本

依赖：
- maptile（核心库）
- lvgl

CMake 使用方式：
```cmake
add_subdirectory(path/to/maptile/backend/lvgl)
target_link_libraries(my_app PRIVATE maptile_lvgl)
```

### 4.3 maptile/backend/plutovg（PlutoVG 后端）

职责：实现 `VectorGfx` 和 `TextGfx` 的 PlutoVG 版本

依赖：
- maptile（核心库）
- plutovg

### 4.4 test（单元测试）

职责：核心库和 stroke-to-path 算法的单元测试

依赖：
- maptile（核心库）
- 测试框架（如 Google Test 或轻量级 assert）

测试范围：
- `test_stroke_to_path.cpp`：矩形展开、cap/join、退化情况
- `test_tile_parser.cpp`：protobuf 解析、子路径分割
- `test_map_style.cpp`：样式查询、zoom 映射

### 4.5 example（示例应用）

职责：SDL 模拟器 + 地图渲染演示

依赖：
- maptile + maptile_lvgl + maptile_plutovg
- lvgl + freetype + SDL2

这是唯一依赖 lvgl 的地方（除了 maptile_lvgl）。`lv_conf.h` 只存在于 example 目录。

## 5. 顶层 CMakeLists.txt 设计

```cmake
cmake_minimum_required(VERSION 3.14)
project(X-MAP)

# 三方库
add_subdirectory(external/freetype)
add_subdirectory(external/plutovg)
find_package(Protobuf REQUIRED)

# 核心库
add_subdirectory(maptile)

# 可选后端
add_subdirectory(maptile/backend/lvgl)
add_subdirectory(maptile/backend/plutovg)

# 单元测试（可通过 option 关闭）
option(XMAP_BUILD_TESTS "Build unit tests" ON)
if(XMAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# 示例应用（可通过 option 关闭）
option(XMAP_BUILD_EXAMPLE "Build example application" ON)
if(XMAP_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()
```

## 6. 其他项目嵌入方式

### 方式 A：作为 submodule

```cmake
# 宿主项目的 CMakeLists.txt
add_subdirectory(third_party/X-MAP/maptile)
add_subdirectory(third_party/X-MAP/maptile/backend/lvgl)  # 如果用 LVGL
target_link_libraries(my_app PRIVATE maptile maptile_lvgl)
```

### 方式 B：只用核心库 + 自定义后端

```cmake
add_subdirectory(third_party/X-MAP/maptile)
target_link_libraries(my_app PRIVATE maptile)
# 自己实现 VectorGfx/TextGfx 接口
```

### 方式 C：完整引入

```cmake
add_subdirectory(third_party/X-MAP)
# 自动构建 maptile + 所有后端
```

## 7. 命名规范

| 当前 | 重构后 | 原因 |
|------|--------|------|
| `MapTile/` | `maptile/` | 小写目录名，符合 C/C++ 库惯例 |
| `MapTile/gfx/` | `maptile/include/maptile/gfx/` | 公开接口放 include，内部实现放 src |
| `MapTileTypes.h` | `maptile/include/maptile/MapTileTypes.h` | include 路径带命名空间前缀，避免冲突 |
| `MapTileStyleNew.h` → `MapTileStyleBase.h` | 保持 `MapTileStyleBase.h` | 已经改过了 |
| `app_entry.cpp` / `main.cpp` | `example/` 下 | 示例代码不属于库 |
| `lv_conf.h` | `example/lv_conf.h` | LVGL 配置是示例应用的，不是库的 |
| `lvgl/` | `external/lvgl/` 或由宿主提供 | lvgl 不应该在根目录，应该和其他三方库一样在 external 下，或者由嵌入方自己提供 |
| `py_mvt_render/` | `tools/py_mvt_render/` | 开发工具统一放 tools |

## 8. lvgl submodule 的处理

当前 `lvgl/` 在根目录，嵌入其他项目时会冲突（宿主项目可能已经有自己的 lvgl）。

方案：
- 将 `lvgl` submodule 移到 `external/lvgl`，和其他三方库一致
- `maptile_lvgl` 模块不自带 lvgl，通过 CMake 的 `find_package` 或 `target_link_libraries` 让使用方提供
- example 目录引用 `external/lvgl`

这样宿主项目可以用自己的 lvgl 版本，不会冲突。

## 9. 文件命名整改

### 9.1 当前问题

| 文件 | 问题 |
|------|------|
| `MapTileDefs.h` | 只定义了一个 `FeatureProp` typedef，且依赖 `vector_tile.pb.h`，应合并 |
| `MapTileTypes.h` | 引擎无关的基础类型，和 `MapTileDefs.h` 职责重叠 |
| `MapTileStyleBase.h` | 重构残留命名，`StyleBase` 就够了 |
| `MapTileUtils.h/cpp` | 带 `MapTile` 前缀但其他文件不带，不统一 |
| `MapStyle.h/cpp` | 不带前缀，和带前缀的混在一起 |
| `TileParser.h` | 不带前缀 |
| `TileRenderer.h` | 不带前缀 |
| `StrokeToPath.h` | 不带前缀 |

### 9.2 命名规则

采用统一的小写下划线风格（snake_case），去掉 `MapTile` 前缀（因为已经在 `maptile/` 目录下，前缀冗余）：

### 9.3 文件重命名映射

| 当前文件 | 重命名为 | 说明 |
|---------|---------|------|
| `MapTileDefs.h` | 删除，`FeatureProp` 移入 `types.h` | 只有一个 typedef，不值得单独文件 |
| `MapTileTypes.h` | `types.h` | 引擎无关基础类型 + `FeatureProp` |
| `MapTileStyleBase.h` | `style.h` | 样式接口基类 |
| `MapStyle.h/cpp` | `map_style.h/cpp` | 地图样式具体实现 |
| `MapTileUtils.h/cpp` | `tile_utils.h/cpp` | protobuf geometry 解析工具 |
| `TileParser.h/cpp` | `tile_parser.h/cpp` | 瓦片解析器 |
| `TileRenderer.h/cpp` | `tile_renderer.h/cpp` | 渲染调度器 |
| `StrokeToPath.h/cpp` | `stroke_to_path.h/cpp` | stroke 展开算法 |
| `gfx/VectorGfx.h` | `gfx/vector_gfx.h` | 矢量绘图接口 |
| `gfx/TextGfx.h` | `gfx/text_gfx.h` | 文本绘图接口 |
| `gfx/LvglVectorGfx.h/cpp` | `gfx/lvgl_vector_gfx.h/cpp` | LVGL 矢量后端 |
| `gfx/LvglTextGfx.h/cpp` | `gfx/lvgl_text_gfx.h/cpp` | LVGL 文本后端 |
| `gfx/PlutoVGGfx.h/cpp` | `gfx/plutovg_gfx.h/cpp` | PlutoVG 矢量后端 |
| `gfx/PlutoVGTextGfx.h/cpp` | `gfx/plutovg_text_gfx.h/cpp` | PlutoVG 文本后端 |
| `vector_tile.proto` | `proto/vector_tile.proto` | proto 文件单独放 |

### 9.4 重命名后的 include 路径

```cpp
// 核心库
#include <maptile/types.h>
#include <maptile/tile_parser.h>
#include <maptile/tile_renderer.h>
#include <maptile/style.h>
#include <maptile/map_style.h>
#include <maptile/stroke_to_path.h>
#include <maptile/gfx/vector_gfx.h>
#include <maptile/gfx/text_gfx.h>

// LVGL 后端
#include <maptile_lvgl/lvgl_vector_gfx.h>
#include <maptile_lvgl/lvgl_text_gfx.h>

// PlutoVG 后端
#include <maptile_plutovg/plutovg_gfx.h>
#include <maptile_plutovg/plutovg_text_gfx.h>
```

### 9.5 MapTileDefs.h 合并方案

`MapTileDefs.h` 只有一个 typedef：

```cpp
typedef std::map<std::string, vector_tile::Tile_Value> FeatureProp;
```

这个类型依赖 `vector_tile.pb.h`（protobuf 生成的头文件），而 `types.h` 里的 `Color`、`FillStyle` 等是引擎无关的纯类型。

合并策略：`FeatureProp` 移入 `types.h`，因为它是核心库的基础类型，protobuf 依赖在核心库范围内是允许的（`TileParser` 本身就依赖 protobuf）。

## 10. 迁移步骤

1. 创建 `maptile/include/maptile/` 和 `maptile/src/` 目录结构
2. 移动核心库文件，更新 include 路径
3. 创建 `maptile_lvgl/` 和 `maptile_plutovg/` 模块
4. 创建 `example/` 目录，移入 `main.cpp`、`app_entry.cpp`、`lv_conf.h`、`resource/`
5. 移动 `lvgl` submodule 到 `external/lvgl`
6. 移动 `py_mvt_render` 到 `tools/`
7. 重写各级 CMakeLists.txt
8. 清理根目录散落文件（`test.mvt`、`style.json`）
9. 更新 `.gitmodules`
10. 编译验证
