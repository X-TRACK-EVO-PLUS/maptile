/**
 * @file router_reference.h
 * The A* search as it was before the flat state table, kept to check the
 * new one against it. Tests only.
 */
#ifndef MAPTILE_ROUTER_REFERENCE_H
#define MAPTILE_ROUTER_REFERENCE_H
#include "maptile/router.h"
#include "maptile/routing_graph.h"
namespace MapTile {
RouteResult findRouteReference(RoutingGraph* graph, const RoutePoint& from, const RoutePoint& to,
    int maxIterations, double snapLimitM);
}
#endif
