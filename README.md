# maptile

嵌入式设备上的矢量地图渲染组件：读 MVT 矢量瓦片，按样式把整个视口一次画出来，缩放连续、可旋转。
不依赖 GUI 框架，输出走一个很小的 `Canvas` 接口，后端可以是 LVGL、SVG，或者你自己的。

由 [X-MAP](https://github.com/X-TRACK-OVERFLOW/X-MAP) 演化而来（MIT，HanfG 和 _VIFEXTech），保留原作者署名。
X-TRACK-EVO-PLUS 的地图页用它，也可以给别的产品用。

## 特点

- **连续缩放和旋转**：`Camera` 有浮点 `zoom` 和 `bearing`。瓦片取最近的整数级，几何每帧投影，矢量始终清晰，超出数据最高级时直接放大。
- **整视口渲染**：所有可见瓦片按样式层合并，一个样式层一次绘制调用；折线在屏幕空间按像素容差简化，视口外和过小的要素被剔除。
- **不用 protobuf**：MVT 解码器自带，约 250 行，解出来是紧凑的数组而不是对象树。
- **可选依赖**：zlib（gzip 瓦片）和 sqlite3（MBTiles）都是可选的，没有时只是少两种数据源。样式用自带的小 JSON 解析器，没有 cJSON 依赖。
- **同一套样式多个主题**：颜色写 `"$名字"`，运行时 `Style::setColor()` 换调色板，不用重新加载。
- **标注**：点、线、面要素的名称，线标注沿线每隔 `repeat` 像素重复，按样式顺序抢位置，可以给界面划出禁区；文字始终水平。
- **路径规划**：`Router` 在 `.rng` 路网文件上做 A\*（骑行权重），见 `docs/routing-design.md`。

## 用法

```cpp
#include "maptile/renderer.h"

MapTile::MBTilesSource source;                  // 或 DirectorySource("dir/%d/%d/%d.mvt")
source.open("beijing.mbtiles");

MapTile::Style style;
style.loadFile("style-light.json");
style.setColor("water", MapTile::Color::rgb(0x10202d));   // 换主题

MapTile::Camera cam;
cam.setCenterLonLat(116.3895, 39.9225);
cam.zoom = 16.4f;                               // 连续
cam.bearing = 35;                               // 车头朝向
cam.viewW = 240; cam.viewH = 320;
cam.anchor = { 120, 224 };                      // 骑行位置放在偏下，前方留得多

MapTile::MapRenderer renderer(source, style);
MapTile::RenderOptions opt;
opt.labelKeepOut.push_back({ 0, 0, 240, 40 });  // 顶部面板，标注不要压上去
opt.loadBudget = 2;                             // 每帧最多解码 2 个新瓦片，其余下一帧
renderer.render(canvas, cam, opt);              // canvas 是你的 Canvas 后端
// renderer.stats().tilesPending > 0 时下一帧再画一次
```

路线、定位点、标记这类叠加层由应用自己画：`Camera::toScreen()` / `lonLatToScreen()` 给出屏幕坐标，
`panByScreen()` 处理拖动。

## 后端

| 后端 | 位置 | 用途 |
| --- | --- | --- |
| SVG | `backends/svg` | 单元测试、命令行 `maptile-svg`、给设计稿生成真实地图 |
| LVGL 9.6 | `backends/lvgl` | 设备。用 `lv_draw_vector`（ThorVG），字体由应用提供 |

自己写后端只需实现 `Canvas` 的 6 个函数：`beginFrame`、`fill`、`stroke`、`measureText`、`drawText`、`endFrame`。
坐标都是屏幕像素，一批路径一次交给后端。

## 样式格式（schema 2）

```json
{
  "schema": 2,
  "name": "ride-light",
  "background": "$land",
  "palette": { "land": "#e6e4dd", "water": "#a8c2d0", "halo": "#f5f4f0e6" },
  "layers": [
    { "id": "water", "source": "water_polygons", "type": "fill", "color": "$water" },
    { "id": "primary", "source": "streets", "type": "line",
      "filter": { "kind": ["primary", "primary_link"], "!tunnel": ["true"] },
      "color": "#fffdf6", "width": [[10, 0.8], [16, 6.5], [18, 13.5]], "min_zoom": 10 }
  ],
  "labels": [
    { "id": "road", "source": "street_labels", "filter": { "kind": ["secondary", "tertiary"] },
      "field": "name", "color": "$label", "halo": "$halo", "size": [[14, 9], [17, 11]], "weight": 500,
      "min_zoom": 14, "repeat": 200 }
  ]
}
```

- `layers` 按数组顺序绘制，所以描边（casing）就是在主路前面再放一层更宽更深的线。`type` 是 `fill`（只画面）或 `line`（画线，面要素画轮廓）。
- `source` 是瓦片里的图层名，样例数据是 Shortbread 方案（`streets`、`water_polygons`、`buildings`、`land` 等）。
- `filter`：每个键对应一组允许的值（比较文字形式，数字和布尔也按文字），键之间是“且”，键名前加 `!` 表示不在其中（属性缺失也算）。
- `width` / `size` / `opacity`：一个数字，或 `[[zoom, 值], ...]` 按缩放线性插值，宽度单位是像素。
- `min_zoom`（含）/ `max_zoom`（不含）：该层的可见缩放范围。
- `dash`、`cap`（butt / round / square）、`join`（miter / round / bevel）：线型。
- 标注的 `weight` 是 100–900，由后端映射到字体，配合可变字体可以连续调节。
- 颜色 `#rgb` / `#rrggbb` / `#rrggbbaa`，或 `$调色板名`。

## 构建和测试

```sh
cmake -S . -B build -G Ninja -DMAPTILE_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

依赖：C++14 编译器；zlib、sqlite3 可选（`-DMAPTILE_WITH_ZLIB=OFF` / `-DMAPTILE_WITH_SQLITE=OFF` 关掉）。
作为子项目引入时只构建核心库 `maptile::maptile`。

命令行把一个视图画成 SVG：

```sh
build/maptile-svg --mbtiles tests/data/beijing-mini.mbtiles --style tests/data/style-light.json \
    --center 39.9225,116.3895 --zoom 16 --bearing 30 --size 240x320 --out map.svg
```

`tests/data/beijing-mini.mbtiles` 是从北京样例裁出的 34 个瓦片（OpenStreetMap 数据，ODbL）。

## 限制

- 瓦片在调用 `render()` 的线程里同步读取和解码；用 `loadBudget` 把代价摊到多帧。后台线程预取还没做。
- 标注文字水平摆放，不沿道路弯曲。
- 数据按 Web Mercator 的 WGS-84 处理；`gcj02_to_wgs84()` 留给需要的调用者做坐标转换。

## 许可

MIT，见 `LICENSE`。
