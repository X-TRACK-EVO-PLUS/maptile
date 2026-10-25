# 矢量瓦片 vs 栅格瓦片：优势分析与高阶玩法

## 1. 现状对比

以北京区域 Z12 为例：

| 指标 | MVT (原始 protobuf) | MVT (gzip) | PNG (256×256) |
|------|---------------------|------------|---------------|
| 单瓦片平均大小 | ~28KB | ~20KB | ~20KB |
| 存储总量 (56 tiles) | 1588KB | ~1100KB | ~1100KB |
| 渲染方式 | 客户端实时渲染 | 客户端实时渲染 | 服务端预渲染 |
| CPU 开销 | 高（protobuf 解析 + 光栅化） | 更高（+解压） | 低（仅解码 PNG） |

单纯比大小和渲染速度，矢量瓦片没有优势。PNG 是服务端预渲染好的，客户端只需解码显示。

**矢量瓦片的价值在于 PNG 做不到的能力。**

## 2. 矢量瓦片的核心优势

### 2.1 Overzoom（跨级别复用）

这是矢量瓦片最大的存储优势。

PNG 模式：Z10 到 Z15 需要分别存储每个级别的瓦片图片，总量随 zoom 指数增长。

矢量模式：只存 Z10-Z13 的矢量数据，Z14/Z15 可以直接用 Z13 的数据放大渲染（overzoom）。几何数据是坐标，放大后重新光栅化，不会像 PNG 那样模糊。

```
PNG:   Z10(4) + Z11(16) + Z12(56) + Z13(195) + Z14(780) + Z15(3120) = 4171 tiles
矢量:  Z10(4) + Z11(16) + Z12(56) + Z13(195) = 271 tiles (Z14/Z15 overzoom from Z13)
```

**存储节省：271 vs 4171 tiles，减少 93%。** 这对嵌入式设备的 Flash 容量至关重要。

### 2.2 实时样式切换

```cpp
// 亮色主题
MvtImageDecoder::setStyle(lightStyle);
// 暗色主题 — 同一份数据，不同渲染
MvtImageDecoder::setStyle(darkStyle);
// 导航模式 — 突出道路，弱化建筑
MvtImageDecoder::setStyle(navStyle);
```

PNG 模式下每套样式需要一整套瓦片图片。3 套样式 = 3 倍存储。矢量模式只需一份数据 + 多个 JSON 样式文件（几 KB）。

### 2.3 平滑缩放（Fractional Zoom）

PNG 在两个 zoom 级别之间只能拉伸图片，文字和线条会模糊。矢量瓦片可以在任意缩放比例下重新光栅化，保持清晰。

实现方式：
- 用 Z12 的矢量数据
- `TileRenderer::setTargetSize(384)` 渲染到更大的 buffer
- 显示时缩放到 256×256

文字、道路线条在任何缩放比例下都是锐利的。

### 2.4 地图旋转

PNG 旋转后文字会倒过来，线条会出现锯齿。矢量渲染可以：
- 旋转坐标系后重新光栅化
- 文字始终保持正向（沿道路方向或水平）
- 线条抗锯齿质量不受旋转影响

### 2.5 动态内容过滤

矢量数据包含属性信息（道路类型、POI 类别等），可以实时过滤：

```cpp
// 只显示主干道 + 水域（导航简洁模式）
navStyle.setLayerVisible("buildings", false);
navStyle.setLayerVisible("land", false);
navStyle.setMinRoadClass("primary");
```

PNG 无法做到，因为所有图层已经合并成像素了。

### 2.6 交互查询

点击地图上的要素可以查询属性：

```cpp
// 用户点击了某个位置
auto features = tileParser.queryFeatures(screenX, screenY);
// features[0].layer = "streets"
// features[0].props["name"] = "长安街"
// features[0].props["kind"] = "primary"
```

PNG 模式下点击只能得到像素颜色，无法知道点击了什么。

## 3. 推荐的高阶实现路线

### Phase 1：Overzoom 支持（存储优化）

最大收益，最小改动。

```
存储：只下载 Z0-Z13
显示：Z14 用 Z13 数据 overzoom 渲染
      Z15 用 Z13 数据 overzoom 渲染
```

decoder 实现：
```cpp
// decoder_open 中
if (!file_exists(path)) {
    // 尝试用低一级的瓦片 overzoom
    int parentZ = z - 1;
    int parentX = x / 2;
    int parentY = y / 2;
    // 加载 parent tile，只渲染对应的 1/4 区域
    // 渲染到 256×256，相当于 2x 放大
}
```

### Phase 2：平滑缩放

在 zoom 级别之间做连续缩放，而不是离散跳变。

```cpp
// zoom = 12.5 时
// 用 Z12 数据渲染到 362×362 (256 * 2^0.5)
// 显示时缩放到 256×256
float frac = zoom - floor(zoom);
int renderSize = (int)(256 * pow(2, frac));
renderer.setTargetSize(renderSize);
```

### Phase 3：实时样式切换 + 主题

已有基础（`MvtImageDecoder::setStyle`），需要完善：
- 样式切换动画（渐变过渡）
- 导航模式样式（突出道路）
- 夜间模式自动切换

### Phase 4：要素交互

点击查询 + 高亮显示：
- 解析点击位置对应的瓦片坐标
- 在 ParsedTile 中做 hit test
- 高亮渲染选中的要素

### Phase 5：地图旋转

基于矢量渲染的正确旋转：
- 旋转坐标系后重新渲染
- 文字保持水平
- 需要渲染更大区域（旋转后的 AABB）

## 4. 存储优化建议

### 当前问题

MVT 文件存的是原始 protobuf，没有压缩。Z5 的单个瓦片 604KB，比 PNG 大很多。

### 方案 A：gzip 压缩 + decoder 解压

```
存储：.mvt.gz 文件（压缩比 1.2x-3.6x）
decoder：lv_fs_read → zlib inflate → protobuf parse
依赖：需要 zlib/miniz（~50KB 代码）
```

### 方案 B：只存高 zoom 级别

低 zoom 级别（Z0-Z7）瓦片数量少但单个很大（包含全球数据）。可以：
- Z0-Z7：预渲染为 PNG（8 个文件，总共 ~200KB）
- Z8-Z13：存 MVT 文件（275 个文件，gzip 后 ~1MB）
- Z14+：overzoom from Z13

混合模式，MapViewer 的 `setTilePathFormat` 已经支持。

### 方案 C：Overzoom 减少存储级别

```
只存 Z10 和 Z13：
- Z10-Z11：Z10 数据 overzoom
- Z12-Z13：Z13 数据（或 Z12 overzoom）
- Z14+：Z13 overzoom

从 271 tiles 减少到 ~200 tiles
```

## 5. 商业自行车码表的地图策略

### Garmin Edge 系列

- **格式**：Garmin 私有 `.img` 矢量格式（逆向工程已公开）
- **渲染**：设备端实时矢量渲染，不用栅格瓦片
- **数据**：基于 OSM 的 TopoActive 地图，按大洲/国家分区下载
- **存储**：单个 `.img` 文件包含所有 zoom 级别的矢量数据，欧洲全图约 8-12GB
- **特点**：
  - 支持路径规划和实时导航（矢量数据包含路网拓扑）
  - 多 zoom 级别共用一份矢量数据（天然 overzoom）
  - 支持第三方 OSM 地图（OpenStreetMap 社区提供免费 `.img` 转换工具）
  - 2025 年起 Cycle Maps 合并入 TopoActive，统一矢量格式

### Wahoo ELEMNT 系列

- **格式**：Mapsforge `.map` 矢量格式（开源）+ VTM 渲染引擎
- **渲染**：设备端矢量渲染，BOLT V2 起默认使用 VTM（Vector Tile Map）引擎
- **数据**：基于 OSM，按区域下载离线地图
- **特点**：
  - Mapsforge 是专为嵌入式/移动设备设计的开源矢量地图方案
  - 单文件包含多级别矢量数据，支持 XML render theme 自定义样式
  - 社区活跃，有成熟的 OSM → `.map` 转换工具链

### Hammerhead Karoo 系列

- **格式**：Mapbox Vector Tiles（MVT/PBF），运行在 Android 系统上
- **渲染**：基于 Mapbox GL 的矢量渲染（GPU 加速）
- **数据**：OSM 数据，支持离线下载
- **特点**：
  - Android 平台，硬件资源相对充裕
  - Mapbox 样式规范，支持丰富的视觉效果
  - 用户反馈地图样式可读性不如 Garmin（线条区分度不够）

### Bryton / Sigma / iGPSPORT

- **格式**：多数使用预渲染栅格瓦片或简化矢量格式
- **渲染**：低端型号用栅格，高端型号（Sigma ROX 12.1 EVO）用自研矢量渲染
- **特点**：
  - 硬件资源有限，矢量渲染是差异化卖点
  - Sigma 强调"自研地图显示引擎"，突出自行车道和步道

### 行业共识

| 策略 | 说明 |
|------|------|
| **全部矢量** | Garmin、Wahoo、Sigma 高端——一份数据覆盖所有 zoom，支持导航 |
| **单文件存储** | `.img`、`.map` 格式把所有级别打包在一个文件里，不用目录结构 |
| **按区域下载** | 用户选择国家/区域下载，不是全球数据 |
| **样式可定制** | Wahoo 的 render theme、Mapbox 的 style JSON |
| **路网拓扑** | Garmin `.img` 等私有格式在矢量数据之外额外包含路网拓扑层，支持设备端路径规划。MVT 规范本身不含拓扑信息，路径规划需要独立的路网数据（可与 PNG 或 MVT 地图配合使用） |

### 对 X-MAP 的启示

1. **Overzoom 是标配**：所有商业码表都用一份矢量数据覆盖多个 zoom 级别，不会每级存一份
2. **单文件优于目录结构**：`.img` 和 `.map` 都是单文件格式，嵌入式设备上比 `z/x/y.mvt` 目录树更友好（FAT 文件系统目录遍历慢）
3. **压缩是必须的**：商业产品都对矢量数据做了压缩，存储效率是核心竞争力
4. **路径规划与地图渲染解耦**：MVT 不含路网拓扑，路径规划需要独立的路网数据，PNG 和 MVT 地图都能配合使用

## 6. 结论

矢量瓦片在"同等功能"下不比 PNG 小或快。它的价值在于：

1. **Overzoom** — 一份数据覆盖多个 zoom 级别，存储减少 90%+
2. **多样式** — 一份数据支持无限套样式，不需要重复存储
3. **平滑缩放** — 任意缩放比例下保持清晰
4. **交互能力** — 可查询、可过滤、可高亮
5. **地图与导航解耦** — 路径规划需要独立路网数据，和地图格式无关（PNG/MVT 都行）

对于嵌入式设备（Flash 有限、需要离线、需要导航模式切换），这些能力是 PNG 无法替代的。
