/**
 * @file routing_graph.cpp
 */
#include "maptile/routing_graph.h"
#include "maptile/camera.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace MapTile;

RoutingGraph::RoutingGraph()
    : _maxBlocks(8)
    , _zoom(10)
    , _ageCounter(0)
{
}

RoutingGraph::~RoutingGraph()
{
    for (auto& kv : _cache)
        delete kv.second;
}

void RoutingGraph::setDataPath(const char* pathFormat)
{
    _pathFormat = pathFormat;
}

void RoutingGraph::setCacheSize(int maxBlocks)
{
    _maxBlocks = maxBlocks;
}

void RoutingGraph::setCacheBytes(size_t bytes)
{
    _maxBytes = bytes;
}
void RoutingGraph::lonLatToTile(double lon, double lat, int& tileX, int& tileY)
{
    WorldPos w = lonLatToWorld(lon, lat);
    double n = (double)(1 << _zoom);
    tileX = (int)std::floor(w.x * n);
    tileY = (int)std::floor(w.y * n);
}

TileBlock* RoutingGraph::getTile(int tileX, int tileY)
{
    TileKey key = { tileX, tileY };
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        it->second->lruAge = ++_ageCounter;
        return it->second;
    }
    return loadTile(tileX, tileY);
}

TileBlock* RoutingGraph::loadTile(int tileX, int tileY)
{
    char path[512];
    snprintf(path, sizeof(path), _pathFormat.c_str(), _zoom, tileX, tileY);

    FILE* f = fopen(path, "rb");
    if (!f)
        return nullptr;

    RngHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        return nullptr;
    }

    if (memcmp(hdr.magic, "RNG1", 4) != 0) {
        fclose(f);
        return nullptr;
    }

    // Seek past header (in case headerSize > sizeof(RngHeader))
    fseek(f, hdr.headerSize, SEEK_SET);

    auto* block = new TileBlock();
    block->header = hdr;
    block->tileX = tileX;
    block->tileY = tileY;
    block->lruAge = ++_ageCounter;

    // Read nodes
    block->nodes.resize(hdr.nodeCount);
    if (hdr.nodeCount > 0) {
        fread(block->nodes.data(), sizeof(RngNode), hdr.nodeCount, f);
    }

    // Read edges
    block->edges.resize(hdr.edgeCount);
    if (hdr.edgeCount > 0) {
        fread(block->edges.data(), sizeof(RngEdge), hdr.edgeCount, f);
    }

    // Read border nodes
    block->borders.resize(hdr.borderCount);
    if (hdr.borderCount > 0) {
        fread(block->borders.data(), sizeof(RngBorderNode), hdr.borderCount, f);
    }

    fclose(f);
    block->bytes = block->nodes.size() * sizeof(RngNode) + block->edges.size() * sizeof(RngEdge)
        + block->borders.size() * sizeof(RngBorderNode);
    evictIfNeeded(block->bytes);
    TileKey key = { tileX, tileY };
    _cache[key] = block;
    _bytes += block->bytes;
    return block;
}

void RoutingGraph::evictIfNeeded(size_t incoming)
{
    // the search holds on to the tile it is in and, while it crosses a border, to the one beside it
    while ((int)_cache.size() >= _maxBlocks || (_maxBytes && _cache.size() > 2 && _bytes + incoming > _maxBytes)) {
        // Find LRU entry
        auto oldest = _cache.begin();
        for (auto it = _cache.begin(); it != _cache.end(); ++it) {
            if (it->second->lruAge < oldest->second->lruAge)
                oldest = it;
        }
        _bytes -= oldest->second->bytes;
        delete oldest->second;
        _cache.erase(oldest);
    }
}

static bool hasRideableEdge(const TileBlock* block, uint32_t node)
{
    uint32_t k = block->nodes[node].firstEdge;
    while (k != 0xFFFFFFFF && k < (uint32_t)block->edges.size()) {
        const RngEdge& e = block->edges[k];
        if (edgeIsRideable(e.flags))
            return true;
        if (e.nextEdge == 0xFFFF)
            break;
        k += e.nextEdge;
    }
    return false;
}
uint32_t RoutingGraph::findNearestNode(TileBlock* block, int32_t lon, int32_t lat, bool needEdges)
{
    if (!block || block->nodes.empty())
        return UINT32_MAX;

    uint32_t best = UINT32_MAX;
    int64_t bestDist = std::numeric_limits<int64_t>::max();

    for (uint32_t i = 0; i < (uint32_t)block->nodes.size(); i++) {
        if (needEdges && !hasRideableEdge(block, i))
            continue;
        int64_t dx = (int64_t)block->nodes[i].lon - lon;
        int64_t dy = (int64_t)block->nodes[i].lat - lat;
        int64_t d = dx * dx + dy * dy;
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}
