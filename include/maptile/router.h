/**
 * @file router.h
 * A* routing engine for bicycle navigation.
 */
#ifndef MAPTILE_ROUTER_H
#define MAPTILE_ROUTER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace MapTile {

struct RoutePoint {
    double lon;
    double lat;
};

/// Why a route was or was not found
enum class RouteStatus {
    Found,
    NoData, // no routing tile under the start or the end
    NoNodeNearStart, // the nearest road node is further than the snap limit
    NoNodeNearEnd,
    NotConnected, // the search ran out of roads
    TooLong, // the search hit its iteration limit
};

struct RouteResult {
    std::vector<RoutePoint> path;
    uint32_t distance_m; // total distance in meters
    int32_t elevationGain; // total elevation gain in meters
    bool found;
    RouteStatus status = RouteStatus::NotConnected;
    double startSnapM = 0; // metres from the start to the road node the route begins at
    double endSnapM = 0;
    int iterations = 0;
};

class RoutingGraph;

class Router {
public:
    Router();
    ~Router();

    /// Set path format for .rng files, e.g. "example/resource/routing/%d/%d/%d.rng"
    void setDataPath(const char* pathFormat);

    /// Set LRU cache size (number of tile blocks)
    void setCacheSize(int maxBlocks);

    /// Upper bound on the memory of the cached tiles (see RoutingGraph::setCacheBytes)
    void setCacheBytes(size_t bytes);
    /// Longest distance from a start or end point to the road node it snaps to; 0 means no limit
    void setSnapLimit(double metres) { _snapLimitM = metres; }

    /// Upper bound on the A* expansions (the default suits a city)
    void setMaxIterations(int n) { _maxIterations = n; }

    /// Weight of the straight-line distance in the A* estimate. Below 1 the
    /// estimate never overshoots (but the search widens); above 1 it is
    /// faster and the route may be a little longer than the best one.
    void setHeuristicWeight(double w) { _heuristicWeight = w; }
    /// Upper bound on the memory of the A* state table; 0 means no limit.
    /// A search that needs more ends as TooLong.
    void setMaxStateBytes(size_t bytes) { _maxStateBytes = bytes; }
    /// Find a route between two points
    RouteResult findRoute(const RoutePoint& from, const RoutePoint& to);

private:
    RoutingGraph* _graph;
    double _snapLimitM = 0;
    int _maxIterations = 100000;
    double _heuristicWeight = 0.8;
    size_t _maxStateBytes = 0;
};

} // namespace MapTile

#endif // MAPTILE_ROUTER_H
