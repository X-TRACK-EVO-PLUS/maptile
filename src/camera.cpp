/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile/camera.h"
#include <algorithm>

namespace MapTile {

static const double PI = 3.14159265358979323846;
static const double MAX_LAT = 85.0511287798066;

WorldPos lonLatToWorld(double lon, double lat)
{
    lat = std::max(-MAX_LAT, std::min(MAX_LAT, lat));
    double s = std::sin(lat * PI / 180.0);
    WorldPos w;
    w.x = (lon + 180.0) / 360.0;
    w.y = 0.5 - std::log((1.0 + s) / (1.0 - s)) / (4.0 * PI);
    return w;
}

void worldToLonLat(const WorldPos& w, double& lon, double& lat)
{
    lon = w.x * 360.0 - 180.0;
    lat = std::atan(std::sinh(PI * (1.0 - 2.0 * w.y))) * 180.0 / PI;
}

double metresPerWorldUnit(double lat)
{
    return 40075016.685578488 * std::cos(lat * PI / 180.0);
}

double Camera::metresPerPixel() const
{
    double lon, lat;
    worldToLonLat(center, lon, lat);
    return metresPerWorldUnit(lat) / worldToPixel();
}

Vec2 Camera::toScreen(const WorldPos& w) const
{
    double k = worldToPixel();
    Vec2 d { (float)((w.x - center.x) * k), (float)((w.y - center.y) * k) };
    return (Matrix::translate(anchor.x, anchor.y) * Matrix::rotate(-bearing)).apply(d);
}

WorldPos Camera::toWorld(Vec2 s) const
{
    Matrix inv = (Matrix::translate(anchor.x, anchor.y) * Matrix::rotate(-bearing)).inverse();
    Vec2 d = inv.apply(s);
    double k = worldToPixel();
    return { center.x + d.x / k, center.y + d.y / k };
}

void Camera::panByScreen(float dx, float dy)
{
    /* the map follows the finger: the centre moves the other way */
    Matrix inv = Matrix::rotate(-bearing).inverse();
    Vec2 d = inv.apply({ -dx, -dy });
    double k = worldToPixel();
    center.x += d.x / k;
    center.y += d.y / k;
}

} // namespace MapTile
