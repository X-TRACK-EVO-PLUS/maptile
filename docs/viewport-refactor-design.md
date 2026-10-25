# Viewport 与 Tile 视图变换整改方案

## 1. 当前问题

### 1.1 拖拽平移体验差

当前 `MapViewer::onPress` 监听 `LV_EVENT_PRESSING`，通过 `lv_indev_get_vect` 获取拖拽增量，调用 `Viewport::pan()` 修改 `_centerLon/_centerLat`，然后重新计算所有 tile 的屏幕位置。

问题：
- 每次拖拽都要做 经纬度↔世界像素↔屏幕像素 的浮点运算链，累积误差导致地图"漂移"
- 没有利用 LVGL 原生的 scroll 机制，缺少惯性滚动、弹性回弹等交互体验
- `pan()` 里对纬度的转换是非线性的（Mercator），直接用屏幕像素增量换算经纬度偏移，在高纬度区域会有明显误差

### 1.2 Tile 定位方式有问题

当前 `repositionTiles` 对每个 tile 调用 `getTileTransform`，该函数把 tile 左上角转为经纬度再调用 `geoToScreen` 转为屏幕坐标。

问题：
- 每个 tile 独立做 geo→screen 变换，浮点精度导致相邻 tile 之间出现 1px 缝隙或重叠
- `getTileTransform` 返回了 `scale` 但 `repositionTiles` 完全没用它
- 旋转时 `geoToScreen` 对每个 tile 应用旋转矩阵，但 canvas 本身没有旋转，导致视觉错乱

### 1.3 旋转行为不正确

tile canvas 没有旋转变换，只是位置按旋转后的坐标摆放，内容还是正北朝上。先移除旋转功能。

### 1.4 无法复用为通用地图框架

当前 `MapViewer` 把矢量 tile 渲染和视图管理混在一起，无法支持 PNG 栅格瓦片。目标是让 X-MAP 成为 X-TRACK-EVO 的地图子仓库，需要同时支持两种 tile 源。

## 2. 整改目标

1. 直接引入 X-TRACK-EVO 的 TileView + MapConv 框架作为底层视图管理
2. 通过 TileProvider 抽象支持 PNG 栅格和矢量渲染两种 tile 加载模式
3. 对外统一为 256x256 tile 接口，上层无感
4. X-MAP 作为子仓库给 X-TRACK-EVO 使用，X-TRACK-EVO 不再自己维护地图渲染

## 3. 参考架构：X-TRACK-EVO 的 TileView + MapConv

### 3.1 核心思想：世界像素坐标 + LVGL 原生滚动

```
经纬度 → MapConv → 世界像素坐标 (pixelX, pixelY)
                         ↓
                    TileView (纯数学，管理 tile 网格)
                         ↓
                    LVGL scroll (原生滚动容器)
```

- **TileView** 是纯数学类，不依赖 UI 框架，负责：
  - 根据 focus 点计算需要哪些 tile
  - 计算 tile 容器的大小和偏移
  - 计算每个 tile 在容器内的位置
  - 全部整数运算，无浮点误差
- **MapConv** 负责经纬度 ↔ 世界像素坐标转换（基于 Microsoft TileSystem）
- **平移完全交给 LVGL 的 scroll 机制**，自带惯性滚动
- **Tile 容器**大小 = `(屏幕宽/tileSize + 2) * tileSize`，刚好覆盖屏幕 + 一圈缓冲

### 3.2 TileView 的 tile 网格计算

```
focusPos = (fx, fy)  // 世界像素坐标

// 视口四角
viewTL = (fx - screenW/2, fy - screenH/2)

// tile 容器：向下取整到 tileSize 边界
contTL = roundDown(viewTL, tileSize)
contSize = (screenW/tileSize + 2) * tileSize

// 每个 tile 的位置
tilePos[i] = contTL + (i % cols * tileSize, i / cols * tileSize)

// 滚动偏移
scrollOffset = viewTL - contTL
```

### 3.3 与当前方案的对比

| 特性 | X-TRACK-EVO | X-MAP 当前 |
|------|-------------|-----------|
| 视口状态 | 世界像素坐标 (int32) | 经纬度 (double) |
| 平移方式 | LVGL 原生 scroll + 惯性 | 手动 pan() 修改经纬度 |
| Tile 定位 | 整数像素，容器内偏移 | 浮点 geo→screen 变换 |
| Tile 网格 | 固定大小容器 | 每个 tile 独立定位 |
| Zoom 切换 | convertPoint 坐标缩放 | 清空重建 |
| Tile 来源 | PNG 文件 | 矢量渲染 canvas |

## 4. 整改方案

### 4.1 引入 TileView 和 MapConv 到 maptile 库

将 X-TRACK-EVO 的以下文件引入 `maptile/` 目录：

```
maptile/
├── include/maptile/
│   ├── tile_view.h        ← TileView.h (重命名为 snake_case)
│   ├── map_conv.h         ← MapConv.h
│   └── tile_system.h      ← TileSystem.h
├── src/
│   ├── tile_view.cpp      ← TileView.cpp
│   ├── map_conv.cpp       ← MapConv.cpp
│   ├── tile_system.cpp    ← TileSystem.cpp
│   └── gcj02.c            ← gcj02.c
```

这些类是纯数学/坐标计算，不依赖 LVGL，放在核心库里合适。

### 4.2 删除当前 Viewport

当前 `Viewport` 的职责被 TileView + MapConv 完全替代：

| Viewport 功能 | 替代方案 |
|---------------|---------|
| `setCenter(lon, lat)` | `MapConv::getCoordinate()` → `TileView::setFocusPos()` |
| `setZoom(z)` | `MapConv::setLevel()` + `MapConv::convertPoint()` |
| `pan(dx, dy)` | LVGL scroll，不需要 |
| `getVisibleTiles()` | `TileView::getTileNum()` + `getTilePos()` |
| `getTileTransform()` | `TileView::getOffset()` |
| `geoToScreen()` | `MapConv::getCoordinate()` + `TileView::getOffset()` |

### 4.3 虚拟 Image Decoder 方案

不再使用 TileProvider 接口手动管理 draw_buf，而是注册 LVGL 自定义 image decoder，让所有 tile（栅格和矢量）统一通过 `lv_image_set_src` 加载，LVGL image cache 自动管理内存回收。

#### 核心思路

```
MapViewer                    LVGL Image System
   │                              │
   │  lv_image_set_src(img,       │
   │    "/maps/12/3368/2540.mvt") │
   │  ─────────────────────────►  │
   │                              │  MVT decoder->info()  → 返回 256x256 ARGB8888
   │                              │  MVT decoder->open()  → 读文件 + protobuf 解析
   │                              │                         + TileRenderer → draw_buf
   │                              │  (image cache 缓存 draw_buf)
   │                              │
   │  lv_image_set_src(img,       │
   │    "/maps/12/3368/2540.png") │
   │  ─────────────────────────►  │
   │                              │  LVGL 内置 PNG decoder → draw_buf
   │                              │  (image cache 缓存 draw_buf)
```

MVT 文件和 PNG 文件放在同样的目录结构 `z/x/y.ext` 下，decoder 通过文件扩展名 `.mvt` 识别，和 PNG decoder 识别 `.png` 完全一样。不需要 SQLite 数据库，不需要 zlib 解压（MVT 文件存储为原始 protobuf）。

#### 数据准备

下载脚本增加一个模式，把瓦片解压后存为独立 MVT 文件：

```
maps/
├── 10/
│   ├── 842/
│   │   ├── 635.mvt
│   │   └── 636.mvt
│   └── 843/
│       ├── 635.mvt
│       └── 636.mvt
├── 11/
│   └── ...
└── 13/
    └── ...
```

和 PNG 栅格瓦片的目录结构完全一致，只是扩展名不同。

#### 优势

- MapViewer 不需要管理 draw_buf 的分配/释放，LVGL image cache 自动回收
- 矢量 tile 和栅格 tile 对 MapViewer 完全透明，都是 `lv_image_set_src(img, path)`
- 同一个 tile 被多次请求时，image cache 自动命中，不重复渲染
- 内存压力大时，LVGL 自动淘汰最久未使用的 tile

#### 矢量 Tile Decoder 注册

```cpp
// maptile/backend/lvgl/include/maptile_lvgl/mvt_image_decoder.h

namespace MapTile {

class MvtImageDecoder {
public:
    /// 注册 .mvt 文件 decoder 到 LVGL
    /// @param style    渲染样式
    /// @param font     文字字体（可为 NULL）
    /// @param tileSize 瓦片渲染尺寸（默认 256）
    static void install(StyleBase& style, lv_font_t* font, int tileSize = 256);

    /// 注销 decoder
    static void uninstall();
};

} // namespace MapTile
```

decoder 通过文件扩展名 `.mvt` 识别：
- `info()`: 检查文件扩展名是否为 `.mvt`，返回 `{w=256, h=256, cf=ARGB8888}`
- `open()`: `lv_fs_open` 读取文件 → `protobuf ParseFromArray` → `TileParser::parse()` → `TileRenderer::render()` → 返回 `lv_draw_buf`
- `close()`: 由 LVGL image cache 管理

不依赖 SQLite，不依赖 zlib，只需要 protobuf + lv_fs 文件系统抽象。

#### 栅格 Tile

栅格图片 tile 不需要自定义 decoder，直接用 LVGL 内置的 PNG/BMP/JPG decoder：

```cpp
// 栅格 tile
lv_image_set_src(img, "/maps/12/3368/2540.png");

// 矢量 tile（同样的目录结构，只是扩展名不同）
lv_image_set_src(img, "/maps/12/3368/2540.mvt");
```

#### PlutoVG 后端

PlutoVG 后端注册另一个 decoder，识别 `.mvt-pvg` 扩展名或通过 install 参数选择渲染后端。

#### MapViewer 使用方式

```cpp
// 矢量地图
MvtImageDecoder::install(style, font);
viewer->setTilePathFormat("/maps/%d/%d/%d.mvt");

// 栅格地图（不需要注册任何 decoder）
viewer->setTilePathFormat("/maps/%d/%d/%d.png");
```

MapViewer 内部只需要 `snprintf` 拼路径 + `lv_image_set_src`，不关心 tile 怎么加载。

### 4.4 重构 MapViewer

```
┌─ _viewCont (LV_OBJ_FLAG_SCROLLABLE) ─────────────┐
│                                                      │
│  ┌─ _tileCont (固定大小，tile 网格容器) ────────┐  │
│  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐        │  │
│  │  │ img  │ │ img  │ │ img  │ │ img  │        │  │
│  │  │      │ │      │ │      │ │      │        │  │
│  │  └──────┘ └──────┘ └──────┘ └──────┘        │  │
│  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐        │  │
│  │  │ img  │ │ img  │ │ img  │ │ img  │        │  │
│  │  └──────┘ └──────┘ └──────┘ └──────┘        │  │
│  └──────────────────────────────────────────────┘  │
│                                                      │
│  [arrow/marker overlay objects]                      │
└──────────────────────────────────────────────────────┘
```

所有 tile 统一用 `lv_image` + `lv_draw_buf` 显示，TileProvider 只负责往 buf 里填内容。

交互流程（参考 X-TRACK-EVO MapView）：

1. `_viewCont` 设为 scrollable，LVGL 原生处理拖拽 + 惯性
2. 监听 `LV_EVENT_SCROLL`：
   - 从 `lv_obj_get_scroll_x/y` + TileView 反推当前 focus 世界像素坐标
   - 调用 `TileView::setFocusPos()` 更新网格
   - 检查 `isTileRectChanged()`，如果变了则重新加载 tile
3. `_tileCont` 是 `_viewCont` 的子对象，大小由 `TileView::getTileRect()` 决定
4. tile 对象（统一 lv_image）在 `_tileCont` 内用 `TileView::getOffset()` 定位
5. Zoom 切换：`MapConv::convertPoint()` 转换焦点 → 重建 tile 网格
6. 双击缩放：参考 X-TRACK-EVO 的 `LV_EVENT_SHORT_CLICKED` 实现

### 4.5 去掉旋转

移除 `_rotArc` 控件和所有旋转相关代码。后续如需旋转，可对 `_tileCont` 整体做 `lv_obj_set_style_transform_rotation`。

## 5. 目录结构变更

```
maptile/
├── include/maptile/
│   ├── tile_view.h          ← 新增（from X-TRACK-EVO TileView）
│   ├── map_conv.h           ← 新增（from X-TRACK-EVO MapConv）
│   ├── tile_system.h        ← 新增（from X-TRACK-EVO TileSystem）
│   ├── tile_provider.h      ← 新增（TileProvider 接口）
│   ├── types.h
│   ├── style.h
│   ├── def_style.h
│   ├── json_style.h
│   ├── tile_parser.h
│   ├── tile_renderer.h
│   ├── mbtiles_reader.h
│   ├── stroke_to_path.h
│   └── gfx/
│       ├── vector_gfx.h
│       └── text_gfx.h
├── src/
│   ├── tile_view.cpp        ← 新增
│   ├── map_conv.cpp         ← 新增
│   ├── tile_system.cpp      ← 新增
│   ├── gcj02.c              ← 新增
│   ├── ... (现有文件不变)
├── proto/
│   └── vector_tile.proto
│
├── backend/lvgl/
│   ├── include/maptile_lvgl/
│   │   ├── lvgl_vector_gfx.h
│   │   ├── lvgl_text_gfx.h
│   │   └── mvt_image_decoder.h       ← 新增（.mvt 文件 decoder）
│   └── src/
│       ├── lvgl_vector_gfx.cpp
│       ├── lvgl_text_gfx.cpp
│       └── mvt_image_decoder.cpp      ← 新增

├── backend/plutovg/
│   ├── include/maptile_plutovg/
│   │   ├── plutovg_gfx.h
│   │   ├── plutovg_text_gfx.h
│   │   └── plutovg_mvt_image_decoder.h  ← 新增（PlutoVG .mvt decoder）
│   └── src/
│       ├── plutovg_gfx.cpp
│       ├── plutovg_text_gfx.cpp
│       └── plutovg_mvt_image_decoder.cpp ← 新增

example/
├── map_viewer.h              ← 重写（基于 TileView + TileProvider）
├── map_viewer.cpp            ← 重写
```

删除：
- `maptile/include/maptile/viewport.h`
- `maptile/src/viewport.cpp`

### Decoder 放置说明

- `MvtImageDecoder` 放在 `maptile/backend/lvgl/`（LVGL decoder + LVGL 矢量渲染后端）
- `PlutoVGMvtImageDecoder` 放在 `maptile/backend/plutovg/`（LVGL decoder + PlutoVG 渲染后端）
- 栅格图片 tile 不需要自定义 decoder，LVGL 内置 decoder 直接支持
- TileView、MapConv、TileSystem 放在核心库（纯数学，不依赖 UI）
- `TileProvider` 接口不再需要，删除 `maptile/include/maptile/tile_provider.h`
- `MBTilesReader` 保留在核心库（仍可用于工具链或批量导出），但 MapViewer 不再依赖它
- 核心库不再强制依赖 SQLite 和 zlib（改为可选）

## 6. X-TRACK-EVO 集成方案

X-MAP 作为子仓库引入 X-TRACK-EVO 后：

```cmake
# X-TRACK-EVO 的 CMakeLists.txt
add_subdirectory(third_party/X-MAP/maptile)
add_subdirectory(third_party/X-MAP/maptile/backend/lvgl)
target_link_libraries(app PRIVATE maptile maptile_lvgl)
```

X-TRACK-EVO 使用栅格 tile（无需自定义 decoder）：
```cpp
MapViewer* viewer = new MapViewer(width, height, parent);
viewer->setTilePathFormat("/maps/%d/%d/%d.png");
viewer->setLevel(16);
viewer->setFocus(lon, lat);
```

X-MAP example 使用 LVGL 矢量渲染：
```cpp
MvtImageDecoder::install(style, font);
MapViewer* viewer = new MapViewer(width, height, parent);
viewer->setTilePathFormat("/maps/%d/%d/%d.mvt");
viewer->setLevel(12);
viewer->setFocus(lon, lat);
```

三者共享同一套 TileView + MapConv + MapViewer 框架，MapViewer 内部只做 `snprintf` 拼路径 + `lv_image_set_src`。数据目录结构完全一致，只是文件扩展名不同。

## 7. 实施步骤

### Phase 1：引入基础框架
1. 将 TileView、MapConv、TileSystem、gcj02 引入 maptile 核心库
2. 适配命名风格（snake_case 文件名，保持类名不变）
3. 更新 CMakeLists.txt

### Phase 2：MVT Image Decoder
4. 实现 `MvtImageDecoder`（注册 LVGL 自定义 decoder，识别 `.mvt` 扩展名）
5. decoder open 内部：lv_fs_open 读文件 → protobuf 解析 → TileRenderer 渲染 → draw_buf
6. 删除 TileProvider 接口及相关实现文件
7. 更新下载脚本，支持导出为 `z/x/y.mvt` 目录结构（解压后的原始 protobuf）

### Phase 3：重写 MapViewer
8. 基于 TileView + LVGL scroll 重写 MapViewer
9. tile 加载统一用 `lv_image_set_src(img, path)`，通过 `setTilePathFormat` 配置路径模板
10. 去掉旋转，去掉 Viewport

### Phase 4：验证
11. 验证矢量 tile 渲染正常（.mvt decoder 路径）
12. 验证拖拽平移流畅（惯性滚动）
13. 验证 zoom 切换焦点不跳
14. 验证 tile 无缝隙
15. 验证 image cache 自动回收内存

### Phase 5：清理依赖
16. 将 SQLite 和 zlib 改为可选依赖（MBTilesReader 保留但不强制编译）
17. 核心库最小依赖：protobuf + lv_fs
