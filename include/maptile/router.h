/**
 * @file router.h
 * A* routing engine for bicycle navigation.
 */
#ifndef MAPTILE_ROUTER_H
#define MAPTILE_ROUTER_H

#include <cstdint>
#include <vector>

namespace MapTile {

struct RoutePoint {
    double lon;
    double lat;
};

struct RouteResult {
    std::vector<RoutePoint> path;
    uint32_t distance_m; // total distance in meters
    int32_t elevationGain; // total elevation gain in meters
    bool found;
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

    /// Find a route between two points
    RouteResult findRoute(const RoutePoint& from, const RoutePoint& to);

private:
    RoutingGraph* _graph;
};

} // namespace MapTile

#endif // MAPTILE_ROUTER_H
