# 单元测试与 CI 设计方案

## 1. 目标

- 单元测试框架，覆盖 maptile 核心库
- 覆盖率报告（lcov + HTML）
- GitHub Actions CI：代码格式检查 + 编译测试（含 example）+ 单元测试 + 覆盖率上传

## 2. 现状

| 项目 | 状态 |
|------|------|
| 测试框架 | `tests/doctest.h` (v2.4.11) 已就位，header-only |
| 测试用例 | 无 |
| 覆盖率工具 | gcov 已有，lcov 需安装 |
| CI | 无 `.github/workflows/` |
| 代码格式 | `tools/code_format.sh` 已有，支持 `--check` 模式 |
| 构建系统 | CMake，maptile 为 static library，example 依赖 SDL2 + LVGL |

## 3. 测试架构

### 3.1 测试目标

只测 maptile 核心库（纯 C++ 逻辑，无 UI 依赖）：

| 模块 | 测试内容 |
|------|----------|
| TileSystem | LatLong↔Pixel 互转精度、边界值 |
| MapConv | getCoordinate/getGeoPos 往返一致性、zoom 级别切换 |
| TileView | 焦点设置、瓦片矩形计算、瓦片数量 |
| RoutingGraph | .rng 文件加载、节点/边解析、LRU 缓存淘汰、最近节点查找 |
| Router | A* 路径规划正确性、不可达处理、跨块路由 |
| Edge flags | 位域编解码正确性 |

### 3.2 目录结构

```
tests/
├── doctest.h              ← 已有
├── CMakeLists.txt         ← 新增：测试构建配置
├── test_main.cpp          ← 新增：doctest main 入口
├── test_tile_system.cpp   ← 新增
├── test_map_conv.cpp      ← 新增
├── test_tile_view.cpp     ← 新增
├── test_routing_graph.cpp ← 新增
├── test_router.cpp        ← 新增
└── test_edge_flags.cpp    ← 新增
```

### 3.3 CMake 集成

```cmake
# tests/CMakeLists.txt
add_executable(xmap_tests
    test_main.cpp
    test_tile_system.cpp
    test_map_conv.cpp
    test_tile_view.cpp
    test_routing_graph.cpp
    test_router.cpp
    test_edge_flags.cpp
)
target_link_libraries(xmap_tests PRIVATE maptile)
target_include_directories(xmap_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
```

根 CMakeLists.txt 新增：

```cmake
option(XMAP_BUILD_TESTS "Build unit tests" OFF)
if(XMAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_test(NAME xmap_tests COMMAND xmap_tests)
endif()
```

测试默认关闭，CI 中通过 `-DXMAP_BUILD_TESTS=ON` 开启。example 编译不受影响。

### 3.4 覆盖率

CI 中用 `--coverage` 编译 flag（等价于 `-fprofile-arcs -ftest-coverage`），运行测试后用 lcov 收集，genhtml 生成报告。

只收集 `maptile/` 目录的覆盖率，排除 external/、tests/、protobuf 生成代码。

## 4. CI 设计

### 4.1 Workflow 结构

单个 workflow 文件 `.github/workflows/ci.yml`，三个 job 并行：

```
┌─────────────┐  ┌─────────────┐  ┌──────────────────┐
│  format     │  │  build      │  │  test + coverage  │
│             │  │             │  │                   │
│ clang-format│  │ cmake build │  │ cmake -DTESTS=ON  │
│ --check     │  │ (含 example)│  │ --coverage        │
│             │  │             │  │ ctest → lcov      │
└─────────────┘  └─────────────┘  └──────────────────┘
```

### 4.2 format job

```yaml
- uses: actions/checkout@v4
  with:
    submodules: recursive
- run: sudo apt-get install -y clang-format
- run: bash tools/code_format.sh --check
```

直接复用现有脚本的 `--check` 模式。需要小改一下脚本：把 `tests/` 目录也加入格式检查范围。

### 4.3 build job

```yaml
- uses: actions/checkout@v4
  with:
    submodules: recursive
- run: sudo apt-get install -y libprotobuf-dev protobuf-compiler libsdl2-dev libsqlite3-dev
- run: cmake -B build -DXMAP_BUILD_EXAMPLE=ON
- run: cmake --build build -j$(nproc)
```

验证完整编译（含 example + SDL2 + LVGL）通过。

### 4.4 test job

```yaml
- uses: actions/checkout@v4
  with:
    submodules: recursive
- run: sudo apt-get install -y libprotobuf-dev protobuf-compiler libsqlite3-dev lcov
- run: cmake -B build -DXMAP_BUILD_TESTS=ON -DXMAP_BUILD_EXAMPLE=OFF
        -DCMAKE_C_FLAGS="--coverage" -DCMAKE_CXX_FLAGS="--coverage"
- run: cmake --build build -j$(nproc)
- run: ctest --test-dir build --output-on-failure
- run: lcov --capture --directory build/maptile --output-file coverage.info
        --include '*/maptile/src/*' --include '*/maptile/include/*'
- run: lcov --remove coverage.info '*.pb.*' --output-file coverage.info
- uses: codecov/codecov-action@v4  # 或 coveralls
  with:
    file: coverage.info
```

测试 job 不编译 example（不需要 SDL2），只编译 maptile + tests，减少 CI 依赖。

### 4.5 code_format.sh 改动

在 `FORMAT_DIRS` 数组中加入 `tests/`：

```bash
FORMAT_DIRS=(
    "$PROJECT_ROOT/maptile"
    "$PROJECT_ROOT/example"
    "$PROJECT_ROOT/tests"
)
```

## 5. 实施步骤

1. 改造 `tools/code_format.sh`：加入 tests 目录
2. 改造根 `CMakeLists.txt`：加入 `XMAP_BUILD_TESTS` 选项
3. 创建 `tests/CMakeLists.txt` + `test_main.cpp`
4. 编写测试用例（6 个文件）
5. 本地验证：编译 + 运行 + 覆盖率
6. 创建 `.github/workflows/ci.yml`
