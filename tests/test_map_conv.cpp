#include "maptile/map_conv.h"
#include "test_common.h"
#include <cmath>

TEST_CASE("MapConv::getCoordinate basic")
{
    MapConv conv(false, 12);

    // Beijing
    MapConv::Point_t pt = conv.getCoordinate(116.4, 39.9);
    CHECK(pt.x > 0);
    CHECK(pt.y > 0);

    // Different point should give different pixel
    MapConv::Point_t pt2 = conv.getCoordinate(116.5, 39.8);
    CHECK(pt2.x != pt.x);
    CHECK(pt2.y != pt.y);
}

TEST_CASE("MapConv::getGeoPos roundtrip")
{
    MapConv conv(false, 12);

    double lon = 116.4, lat = 39.9;
    MapConv::Point_t pt = conv.getCoordinate(lon, lat);

    double lon2, lat2;
    conv.getGeoPos(pt.x, pt.y, &lon2, &lat2);

    CHECK(std::abs(lon2 - lon) < 0.001);
    CHECK(std::abs(lat2 - lat) < 0.001);
}

TEST_CASE("MapConv::setLevel changes pixel coordinates")
{
    MapConv conv(false, 10);
    MapConv::Point_t pt10 = conv.getCoordinate(116.4, 39.9);

    conv.setLevel(12);
    MapConv::Point_t pt12 = conv.getCoordinate(116.4, 39.9);

    // Zoom 12 has 4x the pixels of zoom 10
    CHECK(pt12.x == doctest::Approx(pt10.x * 4).epsilon(2));
    CHECK(pt12.y == doctest::Approx(pt10.y * 4).epsilon(2));
}

TEST_CASE("MapConv::convertPoint between levels")
{
    MapConv conv(false, 12);
    MapConv::Point_t pt12 = conv.getCoordinate(116.4, 39.9);

    // Convert from level 10 coords to level 12
    conv.setLevel(10);
    MapConv::Point_t pt10 = conv.getCoordinate(116.4, 39.9);

    conv.setLevel(12);
    MapConv::Point_t converted = conv.convertPoint(pt10.x, pt10.y, 10);

    CHECK(std::abs(converted.x - pt12.x) <= 2);
    CHECK(std::abs(converted.y - pt12.y) <= 2);
}

TEST_CASE("MapConv::posToTile")
{
    MapConv conv(false, 12);
    MapConv::Tile_t tile = conv.posToTile(512, 768);
    CHECK(tile.tileX == 2);
    CHECK(tile.tileY == 3);
    CHECK(tile.subX == 0);
    CHECK(tile.subY == 0);

    tile = conv.posToTile(513, 769);
    CHECK(tile.tileX == 2);
    CHECK(tile.tileY == 3);
    CHECK(tile.subX == 1);
    CHECK(tile.subY == 1);
}

TEST_CASE("MapConv::getLevel / setLevel")
{
    MapConv conv(false, 5);
    CHECK(conv.getLevel() == 5);
    conv.setLevel(15);
    CHECK(conv.getLevel() == 15);
}
