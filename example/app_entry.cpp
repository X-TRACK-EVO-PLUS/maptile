/**
 * @file app_entry.cpp
 */
#include "map_viewer.h"
#include "maptile/json_style.h"
#include "maptile/router.h"
#include "maptile_lvgl/mvt_image_decoder.h"
#include <cstring>
#include <plutovg.h>
#include <vector>

static MapTile::JsonStyle lightStyle;
static MapTile::JsonStyle darkStyle;

/* --- Point selection state machine --- */

struct AppContext {
    MapViewer* viewer;
    MapTile::Router* router;
    MapTile::RoutePoint startPoint;
    enum { PICK_START,
        PICK_END } pickState;
};

static void onPointSelected(double lon, double lat, void* userData)
{
    auto* ctx = (AppContext*)userData;

    if (ctx->pickState == AppContext::PICK_START) {
        ctx->viewer->clearRoute();
        ctx->viewer->clearMarkers();
        ctx->viewer->setStartMarker(lon, lat);
        ctx->startPoint = { lon, lat };
        ctx->pickState = AppContext::PICK_END;
        LV_LOG_USER("Start point set: (%.5f, %.5f)", lon, lat);
    } else {
        ctx->viewer->setEndMarker(lon, lat);
        LV_LOG_USER("End point set: (%.5f, %.5f), routing...", lon, lat);

        MapTile::RouteResult route = ctx->router->findRoute(
            ctx->startPoint, { lon, lat });

        if (route.found) {
            LV_LOG_USER("Route found: %u m, %d points",
                route.distance_m, (int)route.path.size());

            std::vector<RoutePoint> viewerPath;
            viewerPath.reserve(route.path.size());
            for (const auto& pt : route.path)
                viewerPath.push_back({ pt.lon, pt.lat });

            ctx->viewer->setRoutePath(viewerPath);
            ctx->viewer->showRouteInfo(route.distance_m);
        } else {
            LV_LOG_USER("No route found!");
        }

        ctx->pickState = AppContext::PICK_START;
    }
}

/* --- Entry point --- */

int app_entry(int argc, char** argv)
{
    const char* stylePath = "./example/resource/style_light.json";
    const char* tilePath = "A:example/resource/maps/%d/%d/%d.mvt";
    const char* fontPath = "./example/resource/AlibabaPuHuiTi-3-55-Regular.ttf";
    const char* routingPath = "example/resource/routing/%d/%d/%d.rng";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--style") == 0 && i + 1 < argc)
            stylePath = argv[++i];
        if (strcmp(argv[i], "--tiles") == 0 && i + 1 < argc)
            tilePath = argv[++i];
        if (strcmp(argv[i], "--routing") == 0 && i + 1 < argc)
            routingPath = argv[++i];
    }

    lightStyle.loadFromFile(stylePath);
    darkStyle.loadFromFile("./example/resource/style_dark.json");

    plutovg_font_face_t* pvgFace = plutovg_font_face_load_from_file(fontPath, 0);
    if (!pvgFace) {
        LV_LOG_ERROR("Failed to load PlutoVG font: %s", fontPath);
    }

    MapTile::MvtImageDecoder::install(lightStyle, NULL);
    if (pvgFace) {
        MapTile::MvtImageDecoder::setPlutoVGFont(pvgFace, 12.0f);
    }

    MapViewer* viewer = new MapViewer(800, 600, lv_scr_act());
    viewer->setLevelRange(0, 13);
    viewer->mapConv().setLevel(12);
    viewer->setFocus(116.4, 39.9);
    viewer->setTilePathFormat(tilePath);

    static MapTile::Router router;
    router.setDataPath(routingPath);
    router.setCacheSize(8);

    static AppContext ctx;
    ctx.viewer = viewer;
    ctx.router = &router;
    ctx.pickState = AppContext::PICK_START;

    viewer->setOnPointSelected(onPointSelected, &ctx);

    LV_LOG_USER("Click map to set start point, click again to set end point and route");

    return 0;
}
