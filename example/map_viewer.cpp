/**
 * @file map_viewer.cpp
 */
#include "map_viewer.h"
#include <cmath>
#include <cstdio>
#include <cstring>

#define TILE_SIZE 256

MapViewer::MapViewer(uint32_t width, uint32_t height, lv_obj_t* parent)
    : _tileView(width, height, TILE_SIZE)
    , _mapConv(false, 14, TILE_SIZE)
    , _viewCont(nullptr)
    , _tileCont(nullptr)
    , _routeLayer(nullptr)
    , _routeLine(nullptr)
    , _zoomSlider(nullptr)
    , _infoLabel(nullptr)
    , _startMarker(nullptr)
    , _endMarker(nullptr)
    , _startPoint { 0, 0 }
    , _endPoint { 0, 0 }
    , _hasStart(false)
    , _hasEnd(false)
    , _routeInfoBar(nullptr)
    , _pointSelectedCb(nullptr)
    , _pointSelectedUserData(nullptr)
    , _minLevel(0)
    , _maxLevel(18)
    , _animBusy(false)
    , _animStartX(0)
    , _animStartY(0)
    , _animEndX(0)
    , _animEndY(0)
{
    _curTileRect = TileView::Rect_t();
    createUI(parent, width, height);
}

MapViewer::~MapViewer()
{
    if (_viewCont)
        lv_obj_delete(_viewCont);
}

void MapViewer::createTileObj()
{
    TileObj t;

    t.cont = lv_obj_create(_tileCont);
    lv_obj_remove_style_all(t.cont);
    lv_obj_set_size(t.cont, TILE_SIZE, TILE_SIZE);
    lv_obj_clear_flag(t.cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(t.cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(t.cont, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(t.cont, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(t.cont, lv_color_hex(0xE8E0D8), 0);

    t.label = lv_label_create(t.cont);
    lv_label_set_text(t.label, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_color(t.label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_center(t.label);

    t.img = lv_image_create(t.cont);
    lv_obj_remove_style_all(t.img);
    lv_obj_set_size(t.img, TILE_SIZE, TILE_SIZE);

    _tileObjs.push_back(t);
    _tilePaths.push_back("");
}

static lv_obj_t* createMarkerObj(lv_obj_t* parent, lv_color_t color, const char* text)
{
    lv_obj_t* marker = lv_obj_create(parent);
    lv_obj_remove_style_all(marker);
    lv_obj_set_size(marker, 16, 16);
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(marker, color, 0);
    lv_obj_set_style_border_width(marker, 2, 0);
    lv_obj_set_style_border_color(marker, lv_color_white(), 0);
    lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(marker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* lbl = lv_label_create(marker);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    return marker;
}

void MapViewer::createUI(lv_obj_t* parent, uint32_t width, uint32_t height)
{
    _viewCont = lv_obj_create(parent);
    lv_obj_remove_style_all(_viewCont);
    lv_obj_set_size(_viewCont, width, height);
    lv_obj_set_style_bg_opa(_viewCont, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(_viewCont, lv_color_hex(0xE8E0D8), 0);
    lv_obj_set_style_clip_corner(_viewCont, true, 0);
    lv_obj_set_scrollbar_mode(_viewCont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(_viewCont, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(_viewCont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_viewCont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_viewCont, onPress, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(_viewCont, onShortClick, LV_EVENT_SHORT_CLICKED, this);

    _tileCont = lv_obj_create(_viewCont);
    lv_obj_remove_style_all(_tileCont);
    lv_obj_clear_flag(_tileCont, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(_tileCont, LV_OBJ_FLAG_EVENT_BUBBLE);

    _routeLayer = lv_obj_create(_viewCont);
    lv_obj_remove_style_all(_routeLayer);
    lv_obj_clear_flag(_routeLayer, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(_routeLayer, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(_routeLayer, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    /* Markers on route layer */
    _startMarker = createMarkerObj(_routeLayer, lv_color_hex(0x34A853), "S");
    _endMarker = createMarkerObj(_routeLayer, lv_color_hex(0xEA4335), "E");

    uint32_t tileNum = _tileView.getTileNum();
    for (uint32_t i = 0; i < tileNum; i++)
        createTileObj();

    _zoomSlider = lv_slider_create(parent);
    lv_obj_set_size(_zoomSlider, 20, 200);
    lv_obj_align(_zoomSlider, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_slider_set_range(_zoomSlider, _minLevel, _maxLevel);
    lv_slider_set_value(_zoomSlider, _mapConv.getLevel(), LV_ANIM_OFF);
    lv_obj_add_event_cb(_zoomSlider, onZoomChanged, LV_EVENT_VALUE_CHANGED, this);

    _infoLabel = lv_label_create(parent);
    lv_obj_align(_infoLabel, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_text_color(_infoLabel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_infoLabel, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(_infoLabel, lv_color_black(), 0);
    lv_obj_set_style_pad_all(_infoLabel, 4, 0);
    lv_label_set_text(_infoLabel, "");

    /* Route info bar at bottom */
    _routeInfoBar = lv_obj_create(parent);
    lv_obj_remove_style_all(_routeInfoBar);
    lv_obj_set_size(_routeInfoBar, width, 40);
    lv_obj_align(_routeInfoBar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(_routeInfoBar, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(_routeInfoBar, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_pad_hor(_routeInfoBar, 16, 0);
    lv_obj_clear_flag(_routeInfoBar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* infoLbl = lv_label_create(_routeInfoBar);
    lv_obj_set_style_text_color(infoLbl, lv_color_white(), 0);
    lv_obj_center(infoLbl);
    lv_label_set_text(infoLbl, "");
    /* Store the label pointer via user_data on the bar for easy access */
    lv_obj_set_user_data(_routeInfoBar, infoLbl);

    lv_obj_add_flag(_routeInfoBar, LV_OBJ_FLAG_HIDDEN);
}

void MapViewer::setTilePathFormat(const char* fmt)
{
    _tilePathFmt = fmt;
    reloadTiles();
}

void MapViewer::setLevelRange(int minLevel, int maxLevel)
{
    _minLevel = minLevel;
    _maxLevel = maxLevel;
    lv_slider_set_range(_zoomSlider, minLevel, maxLevel);
    lv_slider_set_value(_zoomSlider, _mapConv.getLevel(), LV_ANIM_OFF);
}

void MapViewer::setFocus(double lon, double lat, bool anim)
{
    MapConv::Point_t pt = _mapConv.getCoordinate(lon, lat);
    setFocus(pt.x, pt.y, anim);
}

void MapViewer::setFocus(int32_t pixelX, int32_t pixelY, bool anim)
{
    if (anim) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, this);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_time(&a, 300);
        lv_anim_set_values(&a, 0, 1000);
        lv_anim_set_ready_cb(&a, [](lv_anim_t* anim) {
            ((MapViewer*)anim->var)->_animBusy = false;
        });
        lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
            auto* self = (MapViewer*)var;
            int32_t fx = lv_map(v, 0, 1000, self->_animStartX, self->_animEndX);
            int32_t fy = lv_map(v, 0, 1000, self->_animStartY, self->_animEndY);
            self->setFocusRaw(fx, fy);
        });
        auto cur = _tileView.getFocusPos();
        _animStartX = cur.x;
        _animStartY = cur.y;
        _animEndX = pixelX;
        _animEndY = pixelY;
        _animBusy = true;
        lv_anim_start(&a);
        return;
    }
    if (_animBusy)
        return;
    setFocusRaw(pixelX, pixelY);
}

bool MapViewer::setLevel(int level)
{
    if (level < _minLevel || level > _maxLevel)
        return false;

    TileView::Point_t cur = _tileView.getFocusPos();
    int oldLevel = _mapConv.getLevel();
    _mapConv.setLevel(level);
    MapConv::Point_t newPt = _mapConv.convertPoint(cur.x, cur.y, oldLevel);

    cancelAnim();
    _curTileRect = TileView::Rect_t();
    for (auto& p : _tilePaths)
        p.clear();
    setFocusRaw(newPt.x, newPt.y);
    lv_slider_set_value(_zoomSlider, level, LV_ANIM_OFF);
    return true;
}

void MapViewer::setFocusRaw(int32_t x, int32_t y)
{
    _tileView.setFocusPos(x, y);

    if (isTileRectChanged()) {
        reloadTiles();
    } else {
        TileView::Point_t offset = _tileView.getTileRectOffset();
        lv_obj_set_pos(_tileCont, -offset.x, -offset.y);
        lv_obj_set_pos(_routeLayer, -offset.x, -offset.y);
        updateInfoLabel();
    }
}

bool MapViewer::isTileRectChanged()
{
    TileView::Rect_t rect = _tileView.getTileRect();
    return memcmp(&_curTileRect, &rect, sizeof(TileView::Rect_t)) != 0;
}

void MapViewer::cancelAnim()
{
    if (!_animBusy)
        return;
    lv_anim_del(this, NULL);
    _animBusy = false;
}

void MapViewer::reloadTiles()
{
    TileView::Rect_t rect = _tileView.getTileRect();
    lv_obj_set_size(_tileCont, rect.width, rect.height);

    uint32_t tileNum = _tileView.getTileNum();

    while (_tileObjs.size() < tileNum)
        createTileObj();

    for (uint32_t i = tileNum; i < _tileObjs.size(); i++)
        lv_obj_add_flag(_tileObjs[i].cont, LV_OBJ_FLAG_HIDDEN);

    int level = _mapConv.getLevel();

    lv_display_t* disp = lv_obj_get_display(_tileCont);
    lv_display_enable_invalidation(disp, false);

    for (uint32_t i = 0; i < tileNum; i++) {
        TileView::Point_t pos = _tileView.getTilePos(i);
        TileView::Point_t imgOffset = _tileView.getOffset(&pos);
        lv_obj_set_pos(_tileObjs[i].cont, imgOffset.x, imgOffset.y);
        lv_obj_clear_flag(_tileObjs[i].cont, LV_OBJ_FLAG_HIDDEN);

        if (!_tilePathFmt.empty()) {
            int tileX = pos.x / TILE_SIZE;
            int tileY = pos.y / TILE_SIZE;
            char path[256];
            snprintf(path, sizeof(path), _tilePathFmt.c_str(), level, tileX, tileY);

            if (_tilePaths[i] != path) {
                _tilePaths[i] = path;
                lv_image_set_src(_tileObjs[i].img, path);

                const void* src = lv_image_get_src(_tileObjs[i].img);
                bool loaded = (src != NULL);
                if (loaded) {
                    lv_obj_add_flag(_tileObjs[i].label, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_image_set_src(_tileObjs[i].img, (const void*)NULL);
                    lv_obj_set_size(_tileObjs[i].img, TILE_SIZE, TILE_SIZE);
                    lv_obj_clear_flag(_tileObjs[i].label, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    lv_display_enable_invalidation(disp, true);
    lv_obj_invalidate(_viewCont);

    _curTileRect = rect;
    updateInfoLabel();

    TileView::Point_t offset = _tileView.getTileRectOffset();
    lv_obj_set_pos(_tileCont, -offset.x, -offset.y);

    lv_obj_set_pos(_routeLayer, -offset.x, -offset.y);
    lv_obj_set_size(_routeLayer, rect.width, rect.height);
    updateRouteLayer();
    updateMarkers();
}

void MapViewer::updateInfoLabel()
{
    TileView::Point_t focus = _tileView.getFocusPos();
    int level = _mapConv.getLevel();
    char buf[128];
    snprintf(buf, sizeof(buf), "Z:%d (%d,%d) T:%d",
        level, focus.x, focus.y, (int)_tileView.getTileNum());
    lv_label_set_text(_infoLabel, buf);
}

/* --- Route display --- */

void MapViewer::setRoutePath(const std::vector<RoutePoint>& path)
{
    _routePoints = path;
    updateRouteLayer();
}

void MapViewer::clearRoute()
{
    _routePoints.clear();
    _routeLinePoints.clear();
    if (_routeLine) {
        lv_obj_delete(_routeLine);
        _routeLine = nullptr;
    }
    hideRouteInfo();
}

void MapViewer::updateRouteLayer()
{
    if (_routePoints.empty()) {
        if (_routeLine)
            lv_obj_add_flag(_routeLine, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    TileView::Rect_t tileRect = _tileView.getTileRect();
    _routeLinePoints.resize(_routePoints.size());

    for (size_t i = 0; i < _routePoints.size(); i++) {
        MapConv::Point_t px = _mapConv.getCoordinate(
            _routePoints[i].lon, _routePoints[i].lat);
        _routeLinePoints[i].x = (lv_value_precise_t)(px.x - tileRect.x);
        _routeLinePoints[i].y = (lv_value_precise_t)(px.y - tileRect.y);
    }

    if (!_routeLine) {
        _routeLine = lv_line_create(_routeLayer);
        lv_obj_remove_style_all(_routeLine);
        lv_obj_set_style_line_color(_routeLine, lv_color_hex(0x4285F4), 0);
        lv_obj_set_style_line_width(_routeLine, 4, 0);
        lv_obj_set_style_line_opa(_routeLine, LV_OPA_80, 0);
        lv_obj_set_style_line_rounded(_routeLine, true, 0);
    }

    lv_obj_clear_flag(_routeLine, LV_OBJ_FLAG_HIDDEN);
    lv_line_set_points_mutable(_routeLine,
        _routeLinePoints.data(), (uint32_t)_routeLinePoints.size());
}

/* --- Point selection --- */

void MapViewer::setOnPointSelected(PointSelectedCb cb, void* userData)
{
    _pointSelectedCb = cb;
    _pointSelectedUserData = userData;
}

void MapViewer::onShortClick(lv_event_t* e)
{
    auto* self = (MapViewer*)lv_event_get_user_data(e);
    if (!self->_pointSelectedCb)
        return;

    lv_indev_t* indev = lv_indev_active();
    if (!indev)
        return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    /* Convert screen coords to map pixel coords */
    lv_coord_t objX = lv_obj_get_x(self->_viewCont);
    lv_coord_t objY = lv_obj_get_y(self->_viewCont);
    int32_t screenX = point.x - objX;
    int32_t screenY = point.y - objY;

    TileView::Point_t focus = self->_tileView.getFocusPos();
    int32_t viewW = self->_tileView.getViewWidth();
    int32_t viewH = self->_tileView.getViewHeight();

    int32_t pixelX = focus.x - viewW / 2 + screenX;
    int32_t pixelY = focus.y - viewH / 2 + screenY;

    double lon, lat;
    self->_mapConv.getGeoPos(pixelX, pixelY, &lon, &lat);

    self->_pointSelectedCb(lon, lat, self->_pointSelectedUserData);
}

/* --- Markers --- */

void MapViewer::setStartMarker(double lon, double lat)
{
    _startPoint = { lon, lat };
    _hasStart = true;
    updateMarkers();
}

void MapViewer::setEndMarker(double lon, double lat)
{
    _endPoint = { lon, lat };
    _hasEnd = true;
    updateMarkers();
}

void MapViewer::clearMarkers()
{
    _hasStart = false;
    _hasEnd = false;
    if (_startMarker)
        lv_obj_add_flag(_startMarker, LV_OBJ_FLAG_HIDDEN);
    if (_endMarker)
        lv_obj_add_flag(_endMarker, LV_OBJ_FLAG_HIDDEN);
}

void MapViewer::updateMarkers()
{
    TileView::Rect_t tileRect = _tileView.getTileRect();

    if (_hasStart && _startMarker) {
        MapConv::Point_t px = _mapConv.getCoordinate(_startPoint.lon, _startPoint.lat);
        lv_obj_set_pos(_startMarker, px.x - tileRect.x - 8, px.y - tileRect.y - 8);
        lv_obj_clear_flag(_startMarker, LV_OBJ_FLAG_HIDDEN);
    }

    if (_hasEnd && _endMarker) {
        MapConv::Point_t px = _mapConv.getCoordinate(_endPoint.lon, _endPoint.lat);
        lv_obj_set_pos(_endMarker, px.x - tileRect.x - 8, px.y - tileRect.y - 8);
        lv_obj_clear_flag(_endMarker, LV_OBJ_FLAG_HIDDEN);
    }
}

/* --- Route info panel --- */

void MapViewer::showRouteInfo(uint32_t distance_m)
{
    if (!_routeInfoBar)
        return;

    char buf[128];
    if (distance_m >= 1000) {
        float km = distance_m / 1000.0f;
        int minutes = (int)(km / 15.0f * 60.0f);
        snprintf(buf, sizeof(buf), "%.1f km    ~%d min", km, minutes);
    } else {
        int minutes = (int)(distance_m / 250.0f);
        if (minutes < 1)
            minutes = 1;
        snprintf(buf, sizeof(buf), "%u m    ~%d min", distance_m, minutes);
    }

    lv_obj_t* lbl = (lv_obj_t*)lv_obj_get_user_data(_routeInfoBar);
    if (lbl)
        lv_label_set_text(lbl, buf);

    lv_obj_clear_flag(_routeInfoBar, LV_OBJ_FLAG_HIDDEN);
}

void MapViewer::hideRouteInfo()
{
    if (_routeInfoBar)
        lv_obj_add_flag(_routeInfoBar, LV_OBJ_FLAG_HIDDEN);
}

/* --- Input events --- */

void MapViewer::onPress(lv_event_t* e)
{
    auto* self = (MapViewer*)lv_event_get_user_data(e);
    lv_indev_t* indev = lv_indev_active();
    if (!indev)
        return;

    lv_point_t vect;
    lv_indev_get_vect(indev, &vect);
    if (vect.x == 0 && vect.y == 0)
        return;

    self->cancelAnim();

    auto cur = self->_tileView.getFocusPos();
    self->setFocusRaw(cur.x - vect.x, cur.y - vect.y);
}

void MapViewer::onZoomChanged(lv_event_t* e)
{
    auto* self = (MapViewer*)lv_event_get_user_data(e);
    int newLevel = lv_slider_get_value(self->_zoomSlider);
    if (newLevel != self->_mapConv.getLevel())
        self->setLevel(newLevel);
}
