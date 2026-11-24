/**
 * @file router.cpp
 * A* routing algorithm implementation.
 */
#include "maptile/router.h"
#include "maptile/routing_graph.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <queue>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#endif

using namespace MapTile;

// --- Edge cost (bicycle-optimized) ---

static const float CLASS_WEIGHT[] = {
    999.0f, // 0: motorway
    3.0f, // 1: trunk
    1.5f, // 2: primary
    1.1f, // 3: secondary
    1.0f, // 4: tertiary
    1.0f, // 5: residential
    0.8f, // 6: cycleway
    1.3f, // 7: path/track
};

static float edgeCost(const RngEdge& e)
{
    int bikeAccess = edgeBicycleAccess(e.flags);
    int roadClass = edgeRoadClass(e.flags);

    if (bikeAccess == 0 || roadClass == 0)
        return std::numeric_limits<float>::infinity();

    float base = (float)e.distance;
    base *= CLASS_WEIGHT[roadClass];

    int surface = edgeSurface(e.flags);
    if (surface == 1)
        base *= 1.5f; // unpaved
    if (surface == 2)
        base *= 2.0f; // gravel

    if (e.elevGain > 0)
        base += (float)e.elevGain * 10.0f;

    if (bikeAccess == 3)
        base *= 0.7f; // designated

    return base;
}

// --- Distance between two points, metres ---
static double metresBetween(double lon1, double lat1, double lon2, double lat2)
{
    double dy = (lat2 - lat1) * 110574.0;
    double dx = (lon2 - lon1) * 111320.0 * cos((lat1 + lat2) * 0.5 * M_PI / 180.0);
    return sqrt(dx * dx + dy * dy);
}

// --- Heuristic ---

/// Straight-line distance in metres (equirectangular, 1 degree = 111320 m).
/// Coordinates are in 1e-7 degrees.
static double straightMetres(int32_t lon1, int32_t lat1, int32_t lon2, int32_t lat2)
{
    double dlat = (double)(lat2 - lat1) * 0.011132; // 111320 / 1e7
    double dlon = (double)(lon2 - lon1) * 0.011132;
    double avgLat = (double)(lat1 + lat2) * 0.5e-7 * M_PI / 180.0;
    dlon *= cos(avgLat);
    return sqrt(dlat * dlat + dlon * dlon);
}

/// Cost of crossing from one tile to the next: the straight distance times
/// 1.25. It is a cost, not a heuristic, so it does not follow the weight.
static float borderCrossingCost(int32_t lon1, int32_t lat1, int32_t lon2, int32_t lat2)
{
    return (float)(straightMetres(lon1, lat1, lon2, lat2) * 0.8) / 0.8f;
}

// --- A* state table ---
//
// One record per node the search has reached, found through an open-addressing
// table. Everything lives in two anonymous mappings, so the memory goes back to
// the system when the search ends (a heap of small blocks would not), and the
// size is known exactly. Records are addressed by index: the arrays move when
// they grow.

namespace {

struct NodeKey {
    uint16_t tileX, tileY;
    uint32_t local;
    bool operator==(const NodeKey& o) const { return tileX == o.tileX && tileY == o.tileY && local == o.local; }
};

struct NodeRecord {
    NodeKey key;
    float g;
    float f;
    uint32_t parent; // index of the record this one was reached from; the top bit is "closed"
};

const uint32_t NO_RECORD = 0x7FFFFFFFu;
const uint32_t CLOSED_BIT = 0x80000000u;

class Mapping {
public:
    Mapping() = default;
    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;
    ~Mapping() { release(); }

    /// Zero-filled memory, or false
    bool allocate(size_t bytes)
    {
        release();
#if defined(__unix__) || defined(__APPLE__)
        void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED)
            return false;
#else
        void* p = calloc(1, bytes);
        if (!p)
            return false;
#endif
        _p = p;
        _bytes = bytes;
        return true;
    }
    void release()
    {
        if (!_p)
            return;
#if defined(__unix__) || defined(__APPLE__)
        munmap(_p, _bytes);
#else
        free(_p);
#endif
        _p = nullptr;
        _bytes = 0;
    }
    void* data() const { return _p; }
    void swap(Mapping& o)
    {
        std::swap(_p, o._p);
        std::swap(_bytes, o._bytes);
    }

private:
    void* _p = nullptr;
    size_t _bytes = 0;
};

class StateTable {
public:
    /// maxBytes 0 means no limit
    explicit StateTable(size_t maxBytes)
        : _maxBytes(maxBytes)
    {
    }

    bool failed() const { return _failed; }

    NodeRecord& at(uint32_t i) { return _records[i]; }

    uint32_t find(const NodeKey& k) const
    {
        if (!_slots)
            return NO_RECORD;
        for (size_t s = hash(k) & _slotMask;; s = (s + 1) & _slotMask) {
            uint32_t v = _slots[s];
            if (v == 0)
                return NO_RECORD;
            if (_records[v - 1].key == k)
                return v - 1;
        }
    }

    /// Add a record for a key that is not in the table; NO_RECORD when out of memory or budget
    uint32_t insert(const NodeKey& k, float g, float f, uint32_t parent)
    {
        if (_count == _capacity && !grow())
            return NO_RECORD;
        uint32_t idx = _count++;
        _records[idx] = { k, g, f, parent };
        place(idx);
        return idx;
    }

private:
    static size_t hash(const NodeKey& k)
    {
        uint64_t v = ((uint64_t)k.tileX << 48) ^ ((uint64_t)k.tileY << 32) ^ k.local;
        v *= 0x9E3779B97F4A7C15ull;
        return (size_t)(v >> 32);
    }

    void place(uint32_t idx)
    {
        size_t s = hash(_records[idx].key) & _slotMask;
        while (_slots[s] != 0)
            s = (s + 1) & _slotMask;
        _slots[s] = idx + 1;
    }

    bool grow()
    {
        size_t cap = _capacity ? _capacity * 2 : 4096;
        size_t slotCount = cap * 2; // load factor at most one half
        size_t bytes = cap * sizeof(NodeRecord) + slotCount * sizeof(uint32_t);
        if ((_maxBytes && bytes > _maxBytes) || cap > (size_t)CLOSED_BIT - 2) {
            _failed = true;
            return false;
        }
        Mapping records, slots;
        if (!records.allocate(cap * sizeof(NodeRecord)) || !slots.allocate(slotCount * sizeof(uint32_t))) {
            _failed = true;
            return false;
        }
        if (_count)
            memcpy(records.data(), _records, _count * sizeof(NodeRecord));
        _recordMap.swap(records);
        _slotMap.swap(slots);
        _records = (NodeRecord*)_recordMap.data();
        _slots = (uint32_t*)_slotMap.data();
        _capacity = cap;
        _slotMask = slotCount - 1;
        for (uint32_t i = 0; i < _count; i++)
            place(i);
        return true;
    }

    Mapping _recordMap, _slotMap;
    NodeRecord* _records = nullptr;
    uint32_t* _slots = nullptr; // record index + 1; 0 is empty
    size_t _capacity = 0;
    size_t _slotMask = 0;
    uint32_t _count = 0;
    size_t _maxBytes;
    bool _failed = false;
};

struct OpenEntry {
    float fScore;
    uint32_t record;
    bool operator>(const OpenEntry& o) const { return fScore > o.fScore; }
};

} // namespace

// --- Router implementation ---

Router::Router()
    : _graph(new RoutingGraph())
{
}

Router::~Router()
{
    delete _graph;
}

void Router::setDataPath(const char* pathFormat)
{
    _graph->setDataPath(pathFormat);
}

void Router::setCacheSize(int maxBlocks)
{
    _graph->setCacheSize(maxBlocks);
}

void Router::setCacheBytes(size_t bytes)
{
    _graph->setCacheBytes(bytes);
}

RouteResult Router::findRoute(const RoutePoint& from, const RoutePoint& to)
{
    RouteResult result;
    result.distance_m = 0;
    result.elevationGain = 0;
    result.found = false;
    result.status = RouteStatus::NotConnected;
    // Find start and end tiles + nodes
    int startTX, startTY, endTX, endTY;
    _graph->lonLatToTile(from.lon, from.lat, startTX, startTY);
    _graph->lonLatToTile(to.lon, to.lat, endTX, endTY);

    TileBlock* startBlock = _graph->getTile(startTX, startTY);
    TileBlock* endBlock = _graph->getTile(endTX, endTY);
    if (!startBlock || !endBlock) {
        result.status = RouteStatus::NoData;
        return result;
    }
    int32_t fromLon = (int32_t)(from.lon * 1e7);
    int32_t fromLat = (int32_t)(from.lat * 1e7);
    int32_t toLon = (int32_t)(to.lon * 1e7);
    int32_t toLat = (int32_t)(to.lat * 1e7);

    uint32_t startNode = _graph->findNearestNode(startBlock, fromLon, fromLat, true);
    uint32_t endNode = _graph->findNearestNode(endBlock, toLon, toLat, true);

    if (startNode == UINT32_MAX || endNode == UINT32_MAX) {
        result.status = startNode == UINT32_MAX ? RouteStatus::NoNodeNearStart : RouteStatus::NoNodeNearEnd;
        return result;
    }

    result.startSnapM = metresBetween(from.lon, from.lat, startBlock->nodes[startNode].lon * 1e-7,
        startBlock->nodes[startNode].lat * 1e-7);
    result.endSnapM = metresBetween(to.lon, to.lat, endBlock->nodes[endNode].lon * 1e-7,
        endBlock->nodes[endNode].lat * 1e-7);
    if (_snapLimitM > 0) {
        if (result.startSnapM > _snapLimitM) {
            result.status = RouteStatus::NoNodeNearStart;
            return result;
        }
        if (result.endSnapM > _snapLimitM) {
            result.status = RouteStatus::NoNodeNearEnd;
            return result;
        }
    }
    // Tile coordinates are stored in 16 bits
    if (startTX < 0 || startTY < 0 || endTX < 0 || endTY < 0 || startTX > 0xFFFF || startTY > 0xFFFF
        || endTX > 0xFFFF || endTY > 0xFFFF) {
        result.status = RouteStatus::NoData;
        return result;
    }
    const NodeKey startKey = { (uint16_t)startTX, (uint16_t)startTY, startNode };
    const NodeKey endKey = { (uint16_t)endTX, (uint16_t)endTY, endNode };

    // A* search
    StateTable states(_maxStateBytes);
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<OpenEntry>> openSet;

    const int32_t endLon = endBlock->nodes[endNode].lon;
    const int32_t endLat = endBlock->nodes[endNode].lat;
    const double weight = _heuristicWeight;
    auto heuristic = [&](int32_t lon, int32_t lat) {
        return (float)(straightMetres(lon, lat, endLon, endLat) * weight);
    };

    float h = heuristic(startBlock->nodes[startNode].lon, startBlock->nodes[startNode].lat);
    uint32_t startRec = states.insert(startKey, 0.0f, h, NO_RECORD);
    if (startRec == NO_RECORD) {
        result.status = RouteStatus::TooLong;
        return result;
    }
    openSet.push({ h, startRec });

    // Reach a node with cost tentG; false when the table is out of memory
    auto relax = [&](const NodeKey& key, float tentG, int32_t lon, int32_t lat, uint32_t parent) {
        uint32_t idx = states.find(key);
        if (idx != NO_RECORD) {
            NodeRecord& r = states.at(idx);
            if ((r.parent & CLOSED_BIT) || !(tentG < r.g))
                return true;
            float nf = tentG + heuristic(lon, lat);
            r.g = tentG;
            r.f = nf;
            r.parent = parent;
            openSet.push({ nf, idx });
            return true;
        }
        float nf = tentG + heuristic(lon, lat);
        idx = states.insert(key, tentG, nf, parent);
        if (idx == NO_RECORD)
            return false;
        openSet.push({ nf, idx });
        return true;
    };

    int iterations = 0;
    bool outOfMemory = false;
    bool cancelled = false;
    const int MAX_ITERATIONS = _maxIterations;
    while (!openSet.empty() && iterations < MAX_ITERATIONS && !outOfMemory) {
        iterations++;
        if (_cancelCheck && (iterations & 1023) == 0 && _cancelCheck(_cancelContext)) {
            cancelled = true;
            break;
        }
        OpenEntry cur = openSet.top();
        openSet.pop();
        const NodeKey curKey = states.at(cur.record).key;

        if (curKey == endKey) {
            result.found = true;
            break;
        }

        if (states.at(cur.record).parent & CLOSED_BIT)
            continue;
        states.at(cur.record).parent |= CLOSED_BIT;
        const float curG = states.at(cur.record).g;

        TileBlock* block = _graph->getTile(curKey.tileX, curKey.tileY);
        if (!block)
            continue;

        const RngNode& node = block->nodes[curKey.local];

        // Iterate edges from this node
        uint32_t edgeIdx = node.firstEdge;
        while (edgeIdx != 0xFFFFFFFF && edgeIdx < (uint32_t)block->edges.size()) {
            const RngEdge& edge = block->edges[edgeIdx];

            float cost = edgeCost(edge);
            if (cost < std::numeric_limits<float>::infinity()) {
                const RngNode& nNode = block->nodes[edge.target];
                if (!relax({ curKey.tileX, curKey.tileY, edge.target }, curG + cost, nNode.lon, nNode.lat, cur.record)) {
                    outOfMemory = true;
                    break;
                }
            }

            // Next edge in linked list
            if (edge.nextEdge == 0xFFFF)
                break;
            edgeIdx += edge.nextEdge;
        }
        if (outOfMemory)
            break;

        // Check border nodes for cross-tile expansion
        for (const auto& border : block->borders) {
            if (border.localNodeId == curKey.local) {
                int remoteTX = border.remoteTileX;
                int remoteTY = border.remoteTileY;
                if (remoteTX < 0 || remoteTY < 0 || remoteTX > 0xFFFF || remoteTY > 0xFFFF)
                    continue;
                TileBlock* remoteBlock = _graph->getTile(remoteTX, remoteTY);
                if (!remoteBlock)
                    continue;

                uint32_t remoteNode = _graph->findNearestNode(
                    remoteBlock, border.remoteLon, border.remoteLat);
                if (remoteNode == UINT32_MAX)
                    continue;

                float crossDist = borderCrossingCost(node.lon, node.lat, border.remoteLon, border.remoteLat);
                const RngNode& rNode = remoteBlock->nodes[remoteNode];
                if (!relax({ (uint16_t)remoteTX, (uint16_t)remoteTY, remoteNode }, curG + crossDist,
                        rNode.lon, rNode.lat, cur.record)) {
                    outOfMemory = true;
                    break;
                }
            }
        }
    }

    result.iterations = iterations;
    if (cancelled) {
        result.status = RouteStatus::Cancelled;
        return result;
    }
    if (!result.found) {
        result.status = (openSet.empty() && !outOfMemory) ? RouteStatus::NotConnected : RouteStatus::TooLong;
        return result;
    }
    result.status = RouteStatus::Found;
    // Reconstruct path
    std::vector<NodeKey> pathKeys;
    uint32_t rec = states.find(endKey);
    result.distance_m = (uint32_t)states.at(rec).g;
    while (rec != NO_RECORD) {
        pathKeys.push_back(states.at(rec).key);
        uint32_t parent = states.at(rec).parent & ~CLOSED_BIT;
        rec = parent;
    }
    std::reverse(pathKeys.begin(), pathKeys.end());

    // Convert to lon/lat
    for (const auto& k : pathKeys) {
        TileBlock* block = _graph->getTile(k.tileX, k.tileY);
        if (!block)
            continue;
        const RngNode& n = block->nodes[k.local];
        result.path.push_back({ n.lon * 1e-7, n.lat * 1e-7 });
    }

    return result;
}
