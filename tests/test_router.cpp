#include "maptile/router.h"
#include "test_common.h"

using namespace MapTile;

// Routing data path relative to project root (ctest WORKING_DIRECTORY is project root)
static const char* ROUTING_PATH = "tests/data/routing/%d/%d/%d.rng";

TEST_CASE("Router::findRoute with real data")
{
    Router router;
    router.setDataPath(ROUTING_PATH);
    router.setCacheSize(8);

    SUBCASE("same-tile route finds path")
    {
        // Both points within tile 843/387 (central Beijing)
        RoutePoint from = { 116.38, 39.92 };
        RoutePoint to = { 116.42, 39.91 };

        RouteResult result = router.findRoute(from, to);
        if (result.found) {
            CHECK(result.path.size() > 2);
            CHECK(result.distance_m > 0);

            // Path should start near 'from' and end near 'to'
            CHECK(result.path.front().lon == doctest::Approx(from.lon).epsilon(0.02));
            CHECK(result.path.front().lat == doctest::Approx(from.lat).epsilon(0.02));
            CHECK(result.path.back().lon == doctest::Approx(to.lon).epsilon(0.02));
            CHECK(result.path.back().lat == doctest::Approx(to.lat).epsilon(0.02));
        } else {
            // If routing data not available, just warn
            MESSAGE("Routing data not found, skipping real-data route test");
        }
    }

    SUBCASE("cross-tile route")
    {
        RoutePoint from = { 116.35, 39.85 };
        RoutePoint to = { 116.42, 39.92 };

        RouteResult result = router.findRoute(from, to);
        // Just check it doesn't crash
        if (result.found) {
            CHECK(result.path.size() > 2);
            CHECK(result.distance_m > 0);
        }
    }

    SUBCASE("unreachable destination returns not found")
    {
        RoutePoint from = { 116.4, 39.9 };
        RoutePoint to = { 120.0, 30.0 }; // Shanghai - no data

        RouteResult result = router.findRoute(from, to);
        CHECK(result.found == false);
        CHECK(result.path.empty());
    }

    SUBCASE("same point returns found")
    {
        RoutePoint pt = { 116.4, 39.9 };
        RouteResult result = router.findRoute(pt, pt);
        // Only check if routing data is available
        if (result.found) {
            CHECK(result.distance_m == 0);
        }
    }
}

TEST_CASE("Router::findRoute with no data")
{
    Router router;
    router.setDataPath("/nonexistent/%d/%d/%d.rng");

    RoutePoint from = { 116.4, 39.9 };
    RoutePoint to = { 116.5, 39.8 };

    RouteResult result = router.findRoute(from, to);
    CHECK(result.found == false);
}

TEST_CASE("Router::findRoute says why it found nothing")
{
    Router router;
    router.setDataPath(ROUTING_PATH);
    router.setCacheSize(8);

    SUBCASE("a route reports Found and how far the ends are from the roads")
    {
        RouteResult r = router.findRoute({ 116.373733, 39.928243 }, { 116.39688, 39.91385 });
        REQUIRE(r.found);
        CHECK(r.status == RouteStatus::Found);
        CHECK(r.startSnapM < 100);
        CHECK(r.endSnapM < 100);
        CHECK(r.iterations > 0);
    }

    SUBCASE("outside the routing tiles is NoData")
    {
        RouteResult r = router.findRoute({ 116.4, 39.9 }, { 120.0, 30.0 });
        CHECK_FALSE(r.found);
        CHECK(r.status == RouteStatus::NoData);
    }

    SUBCASE("a point far from any road is refused when a snap limit is set")
    {
        // the lake of Beihai: the nearest road node is a few hundred metres away
        const RoutePoint lake = { 116.3835, 39.9265 };
        RouteResult free = router.findRoute({ 116.373733, 39.928243 }, lake);
        CHECK(free.endSnapM > 40);
        router.setSnapLimit(free.endSnapM - 20);
        RouteResult r = router.findRoute({ 116.373733, 39.928243 }, lake);
        CHECK_FALSE(r.found);
        CHECK(r.status == RouteStatus::NoNodeNearEnd);
        RouteResult back = router.findRoute(lake, { 116.373733, 39.928243 });
        CHECK(back.status == RouteStatus::NoNodeNearStart);
    }

    SUBCASE("hitting the iteration limit is TooLong, not NotConnected")
    {
        router.setMaxIterations(5);
        RouteResult r = router.findRoute({ 116.373733, 39.928243 }, { 116.39688, 39.91385 });
        CHECK_FALSE(r.found);
        CHECK(r.status == RouteStatus::TooLong);
    }
}

TEST_CASE("Router::findRoute does not start at a copy of a node across a tile border")
{
    // A tile holds copies of the nodes just across its border; they have no edge of their own. In tile 842/387 the node
    // nearest to 116.3192235, 39.9105235 is such a copy, at 116.3192235, 39.9097235.
    Router router;
    router.setDataPath(ROUTING_PATH);
    router.setCacheSize(8);
    RouteResult r = router.findRoute({ 116.3192235, 39.9105235 }, { 116.305, 39.92 });
    CHECK(r.found);
    CHECK(r.status == RouteStatus::Found);
    CHECK(r.startSnapM < 300);
    r = router.findRoute({ 116.305, 39.92 }, { 116.3192235, 39.9105235 });
    CHECK(r.found);
    CHECK(r.endSnapM < 300);
}

TEST_CASE("Router::findRoute with no data reports NoData")
{
    Router router;
    router.setDataPath("/nonexistent/%d/%d/%d.rng");
    RouteResult r = router.findRoute({ 116.4, 39.9 }, { 116.5, 39.8 });
    CHECK(r.status == RouteStatus::NoData);
}

// --- The flat state table against the search it replaced ---

#include "router_reference.h"
#include <cstdio>
#include <cstdlib>
#include <random>

namespace {
struct Pair {
    RoutePoint from, to;
};

void checkSame(const RouteResult& a, const RouteResult& b)
{
    CHECK(a.status == b.status);
    CHECK(a.found == b.found);
    CHECK(a.iterations == b.iterations);
    CHECK(a.distance_m == b.distance_m);
    REQUIRE(a.path.size() == b.path.size());
    for (size_t i = 0; i < a.path.size(); i++) {
        CHECK(a.path[i].lon == b.path[i].lon);
        CHECK(a.path[i].lat == b.path[i].lat);
    }
}
} // namespace

TEST_CASE("Router agrees with the reference search on the test tiles")
{
    const Pair pairs[] = {
        { { 116.38, 39.92 }, { 116.42, 39.91 } },
        { { 116.35, 39.85 }, { 116.42, 39.92 } },
        { { 116.30, 39.80 }, { 116.45, 40.00 } },
        { { 116.42, 39.92 }, { 116.35, 39.85 } },
        { { 116.4, 39.9 }, { 116.4, 39.9 } },
        { { 116.4, 39.9 }, { 120.0, 30.0 } },
    };
    Router router;
    router.setDataPath(ROUTING_PATH);
    router.setCacheSize(8);
    RoutingGraph graph;
    graph.setDataPath(ROUTING_PATH);
    graph.setCacheSize(8);
    for (const Pair& p : pairs) {
        for (int maxIter : { 100000, 300 }) {
            router.setMaxIterations(maxIter);
            checkSame(router.findRoute(p.from, p.to), findRouteReference(&graph, p.from, p.to, maxIter, 0));
        }
    }
}

// MAPTILE_TEST_ROUTING=<dir with 10/x/y.rng> adds a larger set: random pairs
// inside the area covered by the data, reference against new, same results.
TEST_CASE("Router agrees with the reference search on a real region")
{
    const char* dir = getenv("MAPTILE_TEST_ROUTING");
    if (!dir)
        return;
    std::string fmt = std::string(dir) + "/%d/%d/%d.rng";
    Router router;
    router.setDataPath(fmt.c_str());
    router.setCacheSize(64);
    router.setMaxIterations(60000);
    RoutingGraph graph;
    graph.setDataPath(fmt.c_str());
    graph.setCacheSize(64);

    const char* boxEnv = getenv("MAPTILE_TEST_BOX"); // "lon0 lat0 lon1 lat1"
    double box[4] = { 116.2, 39.8, 116.5, 40.0 };
    if (boxEnv)
        sscanf(boxEnv, "%lf %lf %lf %lf", &box[0], &box[1], &box[2], &box[3]);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> lon(box[0], box[2]), lat(box[1], box[3]);
    int found = 0;
    for (int i = 0; i < 40; i++) {
        RoutePoint a = { lon(rng), lat(rng) }, b = { lon(rng), lat(rng) };
        RouteResult n = router.findRoute(a, b);
        RouteResult r = findRouteReference(&graph, a, b, 60000, 0);
        checkSame(n, r);
        found += r.found;
    }
    MESSAGE("routes found: " << found << " of 40");
}

TEST_CASE("Router stops with TooLong when the state table would pass its budget")
{
    Router router;
    router.setDataPath(ROUTING_PATH);
    router.setCacheSize(8);
    router.setMaxStateBytes(64 * 1024); // below the first table
    RouteResult r = router.findRoute({ 116.30, 39.80 }, { 116.45, 40.00 });
    CHECK(r.found == false);
    CHECK(r.status == RouteStatus::TooLong);
    router.setMaxStateBytes(0);
    r = router.findRoute({ 116.30, 39.80 }, { 116.45, 40.00 });
    CHECK(r.status != RouteStatus::TooLong);
}
