#include "maptile/routing_graph.h"
#include "test_common.h"

using namespace MapTile;

TEST_CASE("Edge flag encoding/decoding")
{
    // Build a flags value: oneway=1, road_class=6 (cycleway), surface=2 (gravel),
    // bridge=1, tunnel=0, bicycle_access=3 (designated)
    uint16_t flags = 0;
    flags |= 1; // bit 0: oneway
    flags |= (6 << 1); // bit 1-3: road_class
    flags |= (2 << 4); // bit 4-5: surface
    flags |= (1 << 6); // bit 6: bridge
    // bit 7: tunnel = 0
    flags |= (3 << 8); // bit 8-9: bicycle_access

    CHECK(edgeIsOneway(flags) == true);
    CHECK(edgeRoadClass(flags) == 6);
    CHECK(edgeSurface(flags) == 2);
    CHECK(edgeIsBridge(flags) == true);
    CHECK(edgeIsTunnel(flags) == false);
    CHECK(edgeBicycleAccess(flags) == 3);
}

TEST_CASE("Edge flag zero")
{
    uint16_t flags = 0;
    CHECK(edgeIsOneway(flags) == false);
    CHECK(edgeRoadClass(flags) == 0);
    CHECK(edgeSurface(flags) == 0);
    CHECK(edgeIsBridge(flags) == false);
    CHECK(edgeIsTunnel(flags) == false);
    CHECK(edgeBicycleAccess(flags) == 0);
}

TEST_CASE("Edge flag all bits set")
{
    uint16_t flags = 0x03FF; // bits 0-9 all set
    CHECK(edgeIsOneway(flags) == true);
    CHECK(edgeRoadClass(flags) == 7);
    CHECK(edgeSurface(flags) == 3);
    CHECK(edgeIsBridge(flags) == true);
    CHECK(edgeIsTunnel(flags) == true);
    CHECK(edgeBicycleAccess(flags) == 3);
}

TEST_CASE("Edge flag individual fields")
{
    // Only tunnel set
    uint16_t flags = (1 << 7);
    CHECK(edgeIsOneway(flags) == false);
    CHECK(edgeIsTunnel(flags) == true);
    CHECK(edgeIsBridge(flags) == false);

    // Only bicycle_access = 1 (dismount)
    flags = (1 << 8);
    CHECK(edgeBicycleAccess(flags) == 1);
    CHECK(edgeRoadClass(flags) == 0);
}
