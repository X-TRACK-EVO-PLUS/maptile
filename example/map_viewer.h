/**
 * @file map_viewer.h
 * Interactive map viewer based on TileView + lv_image_set_src
 */
#ifndef MAP_VIEWER_H
#define MAP_VIEWER_H

#include "lvgl/lvgl.h"
#include "maptile/map_conv.h"
#include "maptile/tile_view.h"
#include <string>
#include <vector>

struct RoutePoint {
    double lon;
    double lat;
};

class MapViewer {
public:
    MapViewer(uint32_t width, uint32_t height, lv_obj_t* parent);
    ~MapViewer();

    void setTilePathFormat(const char* fmt);
    void setFocus(double lon, double lat, bool anim = false);
    void setFocus(int32_t pixelX, int32_t pixelY, bool anim = false);
    bool setLevel(int level);
    int getLevel() { return _mapConv.getLevel(); }
    void setLevelRange(int minLevel, int maxLevel);

    /* Route display */
    void setRoutePath(const std::vector<RoutePoint>& path);
    void clearRoute();

    /* Point selection callback */
    typedef void (*PointSelectedCb)(double lon, double lat, void* userData);
    void setOnPointSelected(PointSelectedCb cb, void* userData);

    /* Markers */
    void setStartMarker(double lon, double lat);
    void setEndMarker(double lon, double lat);
    void clearMarkers();

    /* Route info panel */
    void showRouteInfo(uint32_t distance_m);
    void hideRouteInfo();

    lv_obj_t* getViewCont() { return _viewCont; }
    MapConv& mapConv() { return _mapConv; }

private:
    void createUI(lv_obj_t* parent, uint32_t width, uint32_t height);
    void createTileObj();
    void setFocusRaw(int32_t x, int32_t y);
    void reloadTiles();
    bool isTileRectChanged();
    void cancelAnim();
    void updateInfoLabel();
    void updateRouteLayer();
    void updateMarkers();

    static void onPress(lv_event_t* e);
    static void onShortClick(lv_event_t* e);
    static void onZoomChanged(lv_event_t* e);

    TileView _tileView;
    MapConv _mapConv;
    TileView::Rect_t _curTileRect;

    lv_obj_t* _viewCont;
    lv_obj_t* _tileCont;
    lv_obj_t* _routeLayer;
    lv_obj_t* _routeLine;
    lv_obj_t* _zoomSlider;
    lv_obj_t* _infoLabel;

    /* Markers */
    lv_obj_t* _startMarker;
    lv_obj_t* _endMarker;
    RoutePoint _startPoint;
    RoutePoint _endPoint;
    bool _hasStart;
    bool _hasEnd;

    /* Route info bar */
    lv_obj_t* _routeInfoBar;

    /* Point selection */
    PointSelectedCb _pointSelectedCb;
    void* _pointSelectedUserData;

    struct TileObj {
        lv_obj_t* cont;
        lv_obj_t* img;
        lv_obj_t* label;
    };
    std::vector<TileObj> _tileObjs;
    std::vector<std::string> _tilePaths;
    std::string _tilePathFmt;

    std::vector<RoutePoint> _routePoints;
    std::vector<lv_point_precise_t> _routeLinePoints;

    int _minLevel;
    int _maxLevel;

    bool _animBusy;
    int32_t _animStartX, _animStartY;
    int32_t _animEndX, _animEndY;
};

#endif // MAP_VIEWER_H
