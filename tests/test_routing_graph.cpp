#include "maptile/routing_graph.h"
#include "test_common.h"
#include <cstdio>
#include <cstring>

using namespace MapTile;

// Helper: create a minimal .rng file in memory and write to disk
static bool createTestRng(const char* path, int nodeCount, int edgeCount)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;

    RngHeader hdr;
    memcpy(hdr.magic, "RNG1", 4);
    hdr.version = 1;
    hdr.headerSize = sizeof(RngHeader);
    hdr.nodeCount = nodeCount;
    hdr.edgeCount = edgeCount;
    hdr.borderCount = 0;
    hdr.bboxMinLon = 1163000000; // 116.3
    hdr.bboxMinLat = 398000000; // 39.8
    hdr.bboxMaxLon = 1165000000; // 116.5
    hdr.bboxMaxLat = 400000000; // 40.0
    fwrite(&hdr, sizeof(hdr), 1, f);

    // Write nodes: two nodes at known positions
    for (int i = 0; i < nodeCount; i++) {
        RngNode node;
        node.lon = 1164000000 + i * 10000; // slightly different positions
        node.lat = 399000000 + i * 10000;
        node.firstEdge = (i < edgeCount) ? (uint32_t)i : 0xFFFFFFFF;
        fwrite(&node, sizeof(node), 1, f);
    }

    // Write edges: each node connects to the next
    for (int i = 0; i < edgeCount; i++) {
        RngEdge edge;
        edge.target = (i + 1) % nodeCount;
        edge.distance = 100;
        edge.flags = (2 << 1) | (2 << 8); // road_class=2 (primary), bike=yes
        edge.elevGain = 0;
        edge.nextEdge = 0xFFFF; // single edge per node
        fwrite(&edge, sizeof(edge), 1, f);
    }

    fclose(f);
    return true;
}

TEST_CASE("RoutingGraph::loadTile valid file")
{
    const char* path = "/tmp/test_10_100_200.rng";
    REQUIRE(createTestRng(path, 5, 4));

    RoutingGraph graph;
    graph.setDataPath("/tmp/test_%d_%d_%d.rng");
    graph.setCacheSize(4);

    TileBlock* block = graph.getTile(100, 200);
    REQUIRE(block != nullptr);
    CHECK(block->nodes.size() == 5);
    CHECK(block->edges.size() == 4);
    CHECK(block->borders.size() == 0);
    CHECK(block->header.magic[0] == 'R');
    CHECK(block->header.magic[1] == 'N');
    CHECK(block->header.magic[2] == 'G');
    CHECK(block->header.magic[3] == '1');

    remove(path);
}

TEST_CASE("RoutingGraph::loadTile missing file returns nullptr")
{
    RoutingGraph graph;
    graph.setDataPath("/tmp/nonexistent_%d_%d_%d.rng");

    TileBlock* block = graph.getTile(999, 999);
    CHECK(block == nullptr);
}

TEST_CASE("RoutingGraph::getTile caching")
{
    const char* path = "/tmp/test_10_50_60.rng";
    REQUIRE(createTestRng(path, 3, 2));

    RoutingGraph graph;
    graph.setDataPath("/tmp/test_%d_%d_%d.rng");
    graph.setCacheSize(4);

    TileBlock* b1 = graph.getTile(50, 60);
    TileBlock* b2 = graph.getTile(50, 60);
    CHECK(b1 == b2); // same pointer = cached

    remove(path);
}

TEST_CASE("RoutingGraph::LRU eviction")
{
    // Create 3 tile files
    REQUIRE(createTestRng("/tmp/test_10_1_1.rng", 2, 1));
    REQUIRE(createTestRng("/tmp/test_10_2_2.rng", 2, 1));
    REQUIRE(createTestRng("/tmp/test_10_3_3.rng", 2, 1));

    RoutingGraph graph;
    graph.setDataPath("/tmp/test_%d_%d_%d.rng");
    graph.setCacheSize(2); // only 2 slots

    TileBlock* b1 = graph.getTile(1, 1);
    REQUIRE(b1 != nullptr);

    TileBlock* b2 = graph.getTile(2, 2);
    REQUIRE(b2 != nullptr);

    // Loading a 3rd should evict the oldest (b1)
    TileBlock* b3 = graph.getTile(3, 3);
    REQUIRE(b3 != nullptr);

    // b2 should still be cached (was accessed more recently than b1)
    TileBlock* b2_again = graph.getTile(2, 2);
    CHECK(b2_again == b2); // same pointer = still cached

    remove("/tmp/test_10_1_1.rng");
    remove("/tmp/test_10_2_2.rng");
    remove("/tmp/test_10_3_3.rng");
}

TEST_CASE("RoutingGraph::findNearestNode")
{
    const char* path = "/tmp/test_10_10_20.rng";
    REQUIRE(createTestRng(path, 5, 4));

    RoutingGraph graph;
    graph.setDataPath("/tmp/test_%d_%d_%d.rng");

    TileBlock* block = graph.getTile(10, 20);
    REQUIRE(block != nullptr);

    // Node 0 is at (1164000000, 399000000)
    uint32_t nearest = graph.findNearestNode(block, 1164000000, 399000000);
    CHECK(nearest == 0);

    // Node 2 is at (1164020000, 399020000)
    nearest = graph.findNearestNode(block, 1164020000, 399020000);
    CHECK(nearest == 2);

    remove(path);
}

TEST_CASE("RoutingGraph::lonLatToTile")
{
    RoutingGraph graph;

    int tx, ty;
    graph.lonLatToTile(116.4, 39.9, tx, ty);
    // At zoom 10, Beijing should be around tile (843, 387-388)
    CHECK(tx >= 840);
    CHECK(tx <= 845);
    CHECK(ty >= 385);
    CHECK(ty <= 390);
}
