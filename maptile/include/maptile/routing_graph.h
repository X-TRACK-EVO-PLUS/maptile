/**
 * @file routing_graph.h
 * Routing network graph: .rng file loading and LRU block cache.
 */
#ifndef MAPTILE_ROUTING_GRAPH_H
#define MAPTILE_ROUTING_GRAPH_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace MapTile {

/* On-disk structures (packed, little-endian) */

struct RngHeader {
    char magic[4]; // "RNG1"
    uint16_t version;
    uint16_t headerSize;
    uint32_t nodeCount;
    uint32_t edgeCount;
    uint32_t borderCount;
    int32_t bboxMinLon; // degrees * 1e7
    int32_t bboxMinLat;
    int32_t bboxMaxLon;
    int32_t bboxMaxLat;
};

struct RngNode {
    int32_t lon; // degrees * 1e7
    int32_t lat;
    uint32_t firstEdge; // index into edge table, 0xFFFFFFFF = no edges
};

struct RngEdge {
    uint32_t target; // local node index
    uint16_t distance; // meters
    uint16_t flags;
    int16_t elevGain; // meters
    uint16_t nextEdge; // offset to next edge from same source, 0xFFFF = end
};

struct RngBorderNode {
    uint32_t localNodeId;
    uint16_t remoteTileX;
    uint16_t remoteTileY;
    int32_t remoteLon; // degrees * 1e7
    int32_t remoteLat;
};

/* Edge flag accessors */
inline bool edgeIsOneway(uint16_t flags) { return (flags & 0x1) != 0; }
inline int edgeRoadClass(uint16_t flags) { return (flags >> 1) & 0x7; }
inline int edgeSurface(uint16_t flags) { return (flags >> 4) & 0x3; }
inline bool edgeIsBridge(uint16_t flags) { return (flags & (1 << 6)) != 0; }
inline bool edgeIsTunnel(uint16_t flags) { return (flags & (1 << 7)) != 0; }
inline int edgeBicycleAccess(uint16_t flags) { return (flags >> 8) & 0x3; }

/**
 * A loaded tile block.
 */
struct TileBlock {
    RngHeader header;
    std::vector<RngNode> nodes;
    std::vector<RngEdge> edges;
    std::vector<RngBorderNode> borders;
    int tileX, tileY;
    uint32_t lruAge; // for LRU eviction
};

/**
 * Tile key for hash map.
 */
struct TileKey {
    int x, y;
    bool operator==(const TileKey& o) const { return x == o.x && y == o.y; }
};

struct TileKeyHash {
    size_t operator()(const TileKey& k) const
    {
        return std::hash<int>()(k.x) ^ (std::hash<int>()(k.y) << 16);
    }
};

/**
 * Routing graph with LRU tile cache.
 */
class RoutingGraph {
public:
    RoutingGraph();
    ~RoutingGraph();

    void setDataPath(const char* pathFormat);
    void setCacheSize(int maxBlocks);

    /** Get or load a tile block. Returns nullptr if file not found. */
    TileBlock* getTile(int tileX, int tileY);

    /** Find the nearest node in a tile to given lon/lat (1e7). */
    uint32_t findNearestNode(TileBlock* block, int32_t lon, int32_t lat);

    /** Determine which tile a lon/lat point falls in at the configured zoom. */
    void lonLatToTile(double lon, double lat, int& tileX, int& tileY);

    int getZoom() const { return _zoom; }

private:
    TileBlock* loadTile(int tileX, int tileY);
    void evictIfNeeded();

    std::string _pathFormat;
    int _maxBlocks;
    int _zoom;
    uint32_t _ageCounter;
    std::unordered_map<TileKey, TileBlock*, TileKeyHash> _cache;
};

} // namespace MapTile

#endif // MAPTILE_ROUTING_GRAPH_H
