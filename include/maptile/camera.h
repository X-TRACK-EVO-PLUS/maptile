/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_CAMERA_H
#define MAPTILE_CAMERA_H
#include "maptile/geom.h"

namespace MapTile {

/* Web Mercator "world" coordinates: x and y in [0, 1), y grows southwards. */
struct WorldPos {
    double x = 0.5;
    double y = 0.5;
};

WorldPos lonLatToWorld(double lon, double lat);
void worldToLonLat(const WorldPos& w, double& lon, double& lat);
/* Ground distance of one world unit at `lat`, in metres. */
double metresPerWorldUnit(double lat);

/*
 * The view onto the map: a continuous zoom (tile size 256 px at zoom 0 covers
 * the world), a bearing and the screen position that shows the centre.
 * bearing is the compass direction that points up on screen.
 */
struct Camera {
    WorldPos center;
    float zoom = 14.0f;
    float bearing = 0.0f; /* degrees clockwise from north */
    float viewW = 240.0f;
    float viewH = 320.0f;
    Vec2 anchor { 120.0f, 160.0f }; /* screen position of `center` */

    void setCenterLonLat(double lon, double lat) { center = lonLatToWorld(lon, lat); }

    /* Screen pixels per world unit */
    double worldToPixel() const { return 256.0 * std::exp2((double)zoom); }
    /* Metres per screen pixel at the centre latitude */
    double metresPerPixel() const;

    Vec2 toScreen(const WorldPos& w) const;
    WorldPos toWorld(Vec2 screen) const;
    Vec2 lonLatToScreen(double lon, double lat) const { return toScreen(lonLatToWorld(lon, lat)); }
    void screenToLonLat(Vec2 s, double& lon, double& lat) const { worldToLonLat(toWorld(s), lon, lat); }
    /* Move the centre by a screen-space drag (dx, dy) in pixels */
    void panByScreen(float dx, float dy);
};

} // namespace MapTile

#endif // MAPTILE_CAMERA_H
