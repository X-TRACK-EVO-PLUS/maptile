#include "maptile/tile_system.h"
#include "test_common.h"
#include <cmath>

using namespace Microsoft_MapPoint;

TEST_CASE("TileSystem::MapSize")
{
    CHECK(TileSystem::MapSize(1) == 512);
    CHECK(TileSystem::MapSize(10) == 256 * 1024);
    CHECK(TileSystem::MapSize(0) == 256);
}

TEST_CASE("TileSystem::LatLongToPixelXY basic")
{
    int px, py;

    // Origin (0, 0) should be near center of map
    TileSystem::LatLongToPixelXY(0, 0, 1, &px, &py);
    CHECK(px == 256); // center of 512-wide map
    CHECK(py == 256);

    // North pole area
    TileSystem::LatLongToPixelXY(85, 0, 1, &px, &py);
    CHECK(py < 10); // near top

    // South pole area
    TileSystem::LatLongToPixelXY(-85, 0, 1, &px, &py);
    CHECK(py > 500); // near bottom
}

TEST_CASE("TileSystem::PixelXYToLatLong roundtrip")
{
    // Beijing coordinates
    double lat = 39.9, lon = 116.4;
    int level = 12;

    int px, py;
    TileSystem::LatLongToPixelXY(lat, lon, level, &px, &py);

    double lat2, lon2;
    TileSystem::PixelXYToLatLong(px, py, level, &lat2, &lon2);

    // Should be within 1 pixel's worth of error
    double resolution = TileSystem::GroundResolution(lat, level);
    CHECK(std::abs(lat2 - lat) < 0.001); // ~100m tolerance
    CHECK(std::abs(lon2 - lon) < 0.001);
    (void)resolution;
}

TEST_CASE("TileSystem::PixelXYToTileXY")
{
    int tx, ty;
    TileSystem::PixelXYToTileXY(0, 0, &tx, &ty);
    CHECK(tx == 0);
    CHECK(ty == 0);

    TileSystem::PixelXYToTileXY(255, 255, &tx, &ty);
    CHECK(tx == 0);
    CHECK(ty == 0);

    TileSystem::PixelXYToTileXY(256, 256, &tx, &ty);
    CHECK(tx == 1);
    CHECK(ty == 1);
}

TEST_CASE("TileSystem::TileXYToPixelXY")
{
    int px, py;
    TileSystem::TileXYToPixelXY(3, 5, &px, &py);
    CHECK(px == 3 * 256);
    CHECK(py == 5 * 256);
}

TEST_CASE("TileSystem::QuadKey roundtrip")
{
    char buf[32];
    TileSystem::TileXYToQuadKey(3, 5, 3, buf, sizeof(buf));

    int tx, ty, level;
    TileSystem::QuadKeyToTileXY(buf, &tx, &ty, &level);
    CHECK(tx == 3);
    CHECK(ty == 5);
    CHECK(level == 3);
}

TEST_CASE("TileSystem::GroundResolution")
{
    // At equator, zoom 1: map is 512 px wide, earth circumference ~40075km
    double res = TileSystem::GroundResolution(0, 1);
    CHECK(res > 78000); // ~78km per pixel
    CHECK(res < 79000);

    // Higher zoom = smaller resolution
    double res2 = TileSystem::GroundResolution(0, 10);
    CHECK(res2 < res);
}
