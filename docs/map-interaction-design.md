# 地图选点导航交互设计

## 1. 目标

在地图上通过点击选择起点和终点，实时规划路径并显示导航信息（距离、转弯数等）。

## 2. 现状分析

### 2.1 已有能力

| 模块 | 能力 | 状态 |
|------|------|------|
| MapViewer | 拖拽平移、缩放滑块、瓦片加载 | ✅ |
| MapViewer | `setRoutePath()` 路径线叠加显示 | ✅ |
| Router | A* 路径规划，输出 `RouteResult` | ✅ |
| TileSystem | `PixelXYToLatLong` 像素→经纬度 | ✅ 已有但 MapConv 未封装 |
| MapConv | `getCoordinate()` 经纬度→像素 | ✅ |
| MapConv | 像素→经纬度（反向） | ❌ 缺失 |
| LVGL | `LV_EVENT_SHORT_CLICKED` / `LV_EVENT_LONG_PRESSED` | ✅ 可用 |
| 字体 | Font Awesome 图标（`LV_SYMBOL_EXT_*`） | ✅ 有 `LOCATION_DOT`、`FLAG`、`ROUTE` 等 |

### 2.2 关键缺失

1. **MapConv 缺少反向转换**：屏幕点击位置（像素）→ 经纬度。`TileSystem::PixelXYToLatLong` 已存在，只需在 `MapConv` 中封装。

2. **无点击事件区分**：当前 `_viewCont` 只监听 `LV_EVENT_PRESSING`（拖拽），没有区分短按（选点）和拖拽（平移）。

3. **无标记图层**：路径线有了，但起终点标记（pin）还没有。

4. **RouteResult 缺少转弯信息**：当前只返回 `distance_m` 和 `path`，没有分析转弯。

## 3. 交互设计

### 3.1 选点流程

```
用户短按地图 → 第一次：设置起点（绿色标记）
             → 第二次：设置终点（红色标记）→ 自动规划路径
             → 之后再按：清除旧路径，重新设置起点
```

状态机：

```
IDLE → [短按] → START_SET (显示起点标记)
     → [短按] → END_SET (显示终点标记, 触发路由)
     → [短按] → 清除所有, 回到 START_SET
```

### 3.2 拖拽 vs 点击区分

LVGL 已经帮我们区分了：
- `LV_EVENT_PRESSING`：拖拽中持续触发（现有逻辑）
- `LV_EVENT_SHORT_CLICKED`：短按释放，未发生滚动时触发

只需新增 `LV_EVENT_SHORT_CLICKED` 回调，不影响现有拖拽逻辑。

### 3.3 屏幕坐标 → 经纬度

点击事件给出的是相对于 `_viewCont` 的屏幕坐标，转换链路：

```
屏幕坐标 (sx, sy)
  → 全局像素坐标: px = focusX - viewWidth/2 + sx
                   py = focusY - viewHeight/2 + sy
  → 经纬度: TileSystem::PixelXYToLatLong(px, py, level, &lat, &lon)
```

### 3.4 起终点标记

在 `_routeLayer` 上用 `lv_label` 显示 Font Awesome 图标：

| 标记 | 图标 | 颜色 | 符号 |
|------|------|------|------|
| 起点 | `LV_SYMBOL_EXT_LOCATION_DOT` | 绿色 `#34A853` | 实心圆点 |
| 终点 | `LV_SYMBOL_EXT_LOCATION_DOT` | 红色 `#EA4335` | 实心圆点 |

标记跟随地图平移和缩放，与路径线同层。

## 4. 导航信息面板

### 4.1 布局

在地图底部叠加一个半透明信息栏：

```
┌──────────────────────────────────────────┐
│                                          │
│              地图区域                     │
│                                          │
├──────────────────────────────────────────┤
│ 🚴 3.2 km          │     ⏱ ~12 min      │
└──────────────────────────────────────────┘
```

### 4.2 显示内容

| 字段 | 来源 | 计算方式 |
|------|------|----------|
| 距离 | `RouteResult::distance_m` | 直接使用，格式化为 km/m |
| 预计时间 | `distance_m` | 按自行车 15 km/h 估算 |

## 5. 代码改动

### 5.1 MapConv 扩展

```cpp
// maptile/include/maptile/map_conv.h
class MapConv {
    // ... 现有接口 ...

    /// 像素坐标 → 经纬度（反向转换）
    void getGeoPos(int32_t pixelX, int32_t pixelY, double* longitude, double* latitude);
};
```

实现直接调用 `TileSystem::PixelXYToLatLong`。

### 5.2 MapViewer 扩展

```cpp
class MapViewer {
    // ... 现有接口 ...

    /// 设置选点回调
    typedef void (*PointSelectedCb)(double lon, double lat, void* userData);
    void setOnPointSelected(PointSelectedCb cb, void* userData);

    /// 设置/清除起终点标记
    void setStartMarker(double lon, double lat);
    void setEndMarker(double lon, double lat);
    void clearMarkers();

    /// 显示/隐藏导航信息面板
    void showRouteInfo(uint32_t distance_m);
    void hideRouteInfo();

private:
    // 新增
    static void onShortClick(lv_event_t* e);
    void updateMarkers();

    lv_obj_t* _startMarker;  // lv_label with location icon
    lv_obj_t* _endMarker;
    lv_obj_t* _routeInfoBar; // bottom info panel
    lv_obj_t* _routeInfoLabel;

    RoutePoint _startPoint;
    RoutePoint _endPoint;
    bool _hasStart;
    bool _hasEnd;

    PointSelectedCb _pointSelectedCb;
    void* _pointSelectedUserData;
};
```

### 5.3 app_entry 集成

```cpp
// 选点状态机
enum PickState { PICK_START, PICK_END };
static PickState pickState = PICK_START;

void onPointSelected(double lon, double lat, void* ud) {
    auto* ctx = (AppContext*)ud;

    if (pickState == PICK_START) {
        ctx->viewer->clearRoute();
        ctx->viewer->clearMarkers();
        ctx->viewer->setStartMarker(lon, lat);
        ctx->startPoint = {lon, lat};
        pickState = PICK_END;
    } else {
        ctx->viewer->setEndMarker(lon, lat);
        RouteResult route = ctx->router->findRoute(ctx->startPoint, {lon, lat});
        if (route.found) {
            ctx->viewer->setRoutePath(route.path);
            ctx->viewer->showRouteInfo(route.distance_m);
        }
        pickState = PICK_START;
    }
}
```

## 6. 目录结构变更

```
maptile/
├── include/maptile/
│   └── map_conv.h            ← 修改：新增 getGeoPos()
├── src/
│   └── map_conv.cpp          ← 修改：实现 getGeoPos()

example/
├── map_viewer.h              ← 修改：新增选点、标记、信息面板接口
├── map_viewer.cpp            ← 修改：实现上述功能
└── app_entry.cpp             ← 修改：选点状态机 + 路由集成
```

无新增文件，全部是对现有文件的扩展。

## 7. 实施步骤

1. **MapConv::getGeoPos()**：封装 `PixelXYToLatLong`，一行实现
2. **MapViewer 选点事件**：新增 `LV_EVENT_SHORT_CLICKED` 回调，屏幕坐标→经纬度→回调
3. **起终点标记**：在 `_routeLayer` 上创建 `lv_label`，跟随地图移动
4. **导航信息面板**：底部半透明栏，显示距离/时间/转弯
5. **app_entry 状态机**：串联选点→路由→显示
