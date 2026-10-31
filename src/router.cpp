/**
 * @file router.cpp
 * A* routing algorithm implementation.
 */
#include "maptile/router.h"
#include "maptile/routing_graph.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

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

static float heuristic(int32_t lon1, int32_t lat1, int32_t lon2, int32_t lat2)
{
    // Equirectangular approximation in meters
    // 1 degree ≈ 111320m, coordinates are in 1e-7 degrees
    double dlat = (double)(lat2 - lat1) * 0.011132; // 111320 / 1e7
    double dlon = (double)(lon2 - lon1) * 0.011132;
    // cos correction using average latitude
    double avgLat = (double)(lat1 + lat2) * 0.5e-7 * M_PI / 180.0;
    dlon *= cos(avgLat);
    return (float)(sqrt(dlat * dlat + dlon * dlon) * 0.8);
}

// --- A* node state ---

struct GlobalNodeId {
    int tileX, tileY;
    uint32_t localIdx;

    bool operator==(const GlobalNodeId& o) const
    {
        return tileX == o.tileX && tileY == o.tileY && localIdx == o.localIdx;
    }
};

struct GlobalNodeIdHash {
    size_t operator()(const GlobalNodeId& n) const
    {
        size_t h = std::hash<int>()(n.tileX);
        h ^= std::hash<int>()(n.tileY) << 11;
        h ^= std::hash<uint32_t>()(n.localIdx) << 22;
        return h;
    }
};

struct NodeState {
    float gScore;
    float fScore;
    GlobalNodeId parent;
    bool closed;
};

struct OpenEntry {
    float fScore;
    GlobalNodeId id;
    bool operator>(const OpenEntry& o) const { return fScore > o.fScore; }
};

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

    uint32_t startNode = _graph->findNearestNode(startBlock, fromLon, fromLat);
    uint32_t endNode = _graph->findNearestNode(endBlock, toLon, toLat);

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
    GlobalNodeId startId = { startTX, startTY, startNode };
    GlobalNodeId endId = { endTX, endTY, endNode };

    // A* search
    std::unordered_map<GlobalNodeId, NodeState, GlobalNodeIdHash> states;
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<OpenEntry>> openSet;

    int32_t endLon = endBlock->nodes[endNode].lon;
    int32_t endLat = endBlock->nodes[endNode].lat;

    float h = heuristic(startBlock->nodes[startNode].lon,
        startBlock->nodes[startNode].lat,
        endLon, endLat);

    states[startId] = { 0.0f, h, { 0, 0, UINT32_MAX }, false };
    openSet.push({ h, startId });

    int iterations = 0;
    const int MAX_ITERATIONS = _maxIterations;
    while (!openSet.empty() && iterations < MAX_ITERATIONS) {
        iterations++;
        OpenEntry cur = openSet.top();
        openSet.pop();

        if (cur.id == endId) {
            result.found = true;
            break;
        }

        auto& curState = states[cur.id];
        if (curState.closed)
            continue;
        curState.closed = true;

        TileBlock* block = _graph->getTile(cur.id.tileX, cur.id.tileY);
        if (!block)
            continue;

        const RngNode& node = block->nodes[cur.id.localIdx];

        // Iterate edges from this node
        uint32_t edgeIdx = node.firstEdge;
        while (edgeIdx != 0xFFFFFFFF && edgeIdx < (uint32_t)block->edges.size()) {
            const RngEdge& edge = block->edges[edgeIdx];

            float cost = edgeCost(edge);
            if (cost < std::numeric_limits<float>::infinity()) {
                GlobalNodeId neighborId = { cur.id.tileX, cur.id.tileY, edge.target };
                float tentG = curState.gScore + cost;

                auto it = states.find(neighborId);
                if (it == states.end() || (!it->second.closed && tentG < it->second.gScore)) {
                    const RngNode& nNode = block->nodes[edge.target];
                    float nh = heuristic(nNode.lon, nNode.lat, endLon, endLat);
                    float nf = tentG + nh;

                    states[neighborId] = { tentG, nf, cur.id, false };
                    openSet.push({ nf, neighborId });
                }
            }

            // Next edge in linked list
            if (edge.nextEdge == 0xFFFF)
                break;
            edgeIdx += edge.nextEdge;
        }

        // Check border nodes for cross-tile expansion
        for (const auto& border : block->borders) {
            if (border.localNodeId == cur.id.localIdx) {
                int remoteTX = border.remoteTileX;
                int remoteTY = border.remoteTileY;
                TileBlock* remoteBlock = _graph->getTile(remoteTX, remoteTY);
                if (!remoteBlock)
                    continue;

                uint32_t remoteNode = _graph->findNearestNode(
                    remoteBlock, border.remoteLon, border.remoteLat);
                if (remoteNode == UINT32_MAX)
                    continue;

                GlobalNodeId remoteId = { remoteTX, remoteTY, remoteNode };

                // Use straight-line distance as crossing cost
                float crossDist = heuristic(node.lon, node.lat,
                                      border.remoteLon, border.remoteLat)
                    / 0.8f;
                float tentG = curState.gScore + crossDist;

                auto it = states.find(remoteId);
                if (it == states.end() || (!it->second.closed && tentG < it->second.gScore)) {
                    const RngNode& rNode = remoteBlock->nodes[remoteNode];
                    float nh = heuristic(rNode.lon, rNode.lat, endLon, endLat);
                    float nf = tentG + nh;
                    states[remoteId] = { tentG, nf, cur.id, false };
                    openSet.push({ nf, remoteId });
                }
            }
        }
    }

    result.iterations = iterations;
    if (!result.found) {
        result.status = openSet.empty() ? RouteStatus::NotConnected : RouteStatus::TooLong;
        return result;
    }
    result.status = RouteStatus::Found;
    // Reconstruct path
    std::vector<GlobalNodeId> pathIds;
    GlobalNodeId cur = endId;
    while (!(cur.localIdx == UINT32_MAX)) {
        pathIds.push_back(cur);
        auto it = states.find(cur);
        if (it == states.end())
            break;
        cur = it->second.parent;
    }
    std::reverse(pathIds.begin(), pathIds.end());

    // Convert to lon/lat
    result.distance_m = (uint32_t)states[endId].gScore;
    for (const auto& pid : pathIds) {
        TileBlock* block = _graph->getTile(pid.tileX, pid.tileY);
        if (!block)
            continue;
        const RngNode& n = block->nodes[pid.localIdx];
        result.path.push_back({ n.lon * 1e-7, n.lat * 1e-7 });
    }

    return result;
}
