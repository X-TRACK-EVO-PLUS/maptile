#include "maptile/camera.h"
#include "test_common.h"

using namespace MapTile;

TEST_CASE("lon/lat <-> world round trip")
{
    double lon, lat;
    WorldPos w = lonLatToWorld(116.3895, 39.9225);
    worldToLonLat(w, lon, lat);
    CHECK(lon == doctest::Approx(116.3895).epsilon(1e-9));
    CHECK(lat == doctest::Approx(39.9225).epsilon(1e-9));
    /* the equator / prime meridian is the middle of the world */
    WorldPos o = lonLatToWorld(0, 0);
    CHECK(o.x == doctest::Approx(0.5));
    CHECK(o.y == doctest::Approx(0.5));
}

TEST_CASE("Camera: the centre maps to the anchor")
{
    Camera c;
    c.setCenterLonLat(116.39, 39.92);
    c.zoom = 15.3f;
    c.bearing = 37;
    c.anchor = { 120, 224 };
    Vec2 s = c.toScreen(c.center);
    CHECK(s.x == doctest::Approx(120));
    CHECK(s.y == doctest::Approx(224));
}

TEST_CASE("Camera: toWorld inverts toScreen, with rotation")
{
    Camera c;
    c.setCenterLonLat(116.39, 39.92);
    c.zoom = 16.7f;
    c.bearing = 123;
    WorldPos w = lonLatToWorld(116.3921, 39.9187);
    WorldPos back = c.toWorld(c.toScreen(w));
    CHECK(back.x == doctest::Approx(w.x).epsilon(1e-9));
    CHECK(back.y == doctest::Approx(w.y).epsilon(1e-9));
}

TEST_CASE("Camera: north is up at bearing 0, east is up at bearing 90")
{
    Camera c;
    c.setCenterLonLat(116.39, 39.92);
    c.zoom = 16;
    Vec2 north = c.lonLatToScreen(116.39, 39.9215);
    CHECK(north.y < c.anchor.y);
    CHECK(north.x == doctest::Approx(c.anchor.x).epsilon(0.01));
    c.bearing = 90; /* heading east: east is at the top */
    Vec2 east = c.lonLatToScreen(116.3915, 39.92);
    CHECK(east.y < c.anchor.y);
    CHECK(east.x == doctest::Approx(c.anchor.x).epsilon(0.01));
}

TEST_CASE("Camera: metres per pixel")
{
    Camera c;
    c.setCenterLonLat(0, 0);
    c.zoom = 0;
    CHECK(c.metresPerPixel() == doctest::Approx(156543.03).epsilon(1e-4));
    c.zoom = 1;
    CHECK(c.metresPerPixel() == doctest::Approx(78271.52).epsilon(1e-4));
}

TEST_CASE("Camera: dragging moves the map with the finger")
{
    Camera c;
    c.setCenterLonLat(116.39, 39.92);
    c.zoom = 16;
    double lon0, lat0;
    worldToLonLat(c.center, lon0, lat0);
    Vec2 before = c.lonLatToScreen(lon0 + 0.001, lat0);
    c.panByScreen(30, 0);
    Vec2 after = c.lonLatToScreen(lon0 + 0.001, lat0);
    CHECK(after.x - before.x == doctest::Approx(30).epsilon(0.001));
    CHECK(after.y == doctest::Approx(before.y).epsilon(0.001));
    /* and when rotated, the drag follows the screen, not the compass */
    c.bearing = 90;
    Vec2 b2 = c.lonLatToScreen(lon0, lat0 + 0.001);
    c.panByScreen(0, 25);
    Vec2 a2 = c.lonLatToScreen(lon0, lat0 + 0.001);
    CHECK(a2.y - b2.y == doctest::Approx(25).epsilon(0.001));
}
