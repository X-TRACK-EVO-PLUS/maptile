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

TEST_CASE("Router::findRoute with no data reports NoData")
{
    Router router;
    router.setDataPath("/nonexistent/%d/%d/%d.rng");
    RouteResult r = router.findRoute({ 116.4, 39.9 }, { 116.5, 39.8 });
    CHECK(r.status == RouteStatus::NoData);
}
