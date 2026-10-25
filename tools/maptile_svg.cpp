/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 *
 * maptile-svg: render a map view to an SVG file.
 *
 *   maptile-svg --mbtiles map.mbtiles --style style.json --center 39.92,116.39 --zoom 16
 *               [--bearing 30] [--size 240x320] [--color name=#rrggbb ...] [--out map.svg]
 *   maptile-svg --tiles "dir/%d/%d/%d.mvt" ...
 */
#include "maptile/renderer.h"
#include "svg_canvas.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using namespace MapTile;

int main(int argc, char** argv)
{
    std::string mbtiles, pattern, stylePath, out = "map.svg";
    double lat = 39.9225, lon = 116.3895;
    float zoom = 16, bearing = 0, w = 240, h = 320;
    std::vector<std::pair<std::string, std::string>> colors;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--mbtiles")
            mbtiles = next();
        else if (a == "--tiles")
            pattern = next();
        else if (a == "--style")
            stylePath = next();
        else if (a == "--center")
            std::sscanf(next(), "%lf,%lf", &lat, &lon);
        else if (a == "--zoom")
            zoom = (float)std::atof(next());
        else if (a == "--bearing")
            bearing = (float)std::atof(next());
        else if (a == "--size")
            std::sscanf(next(), "%fx%f", &w, &h);
        else if (a == "--out")
            out = next();
        else if (a == "--color") {
            std::string kv = next();
            size_t eq = kv.find('=');
            if (eq != std::string::npos)
                colors.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    if (stylePath.empty() || (mbtiles.empty() && pattern.empty())) {
        std::fprintf(stderr, "usage: maptile-svg (--mbtiles FILE | --tiles PATTERN) --style FILE [--center LAT,LON] "
                             "[--zoom Z] [--bearing DEG] [--size WxH] [--color NAME=#RRGGBB] [--out FILE]\n");
        return 2;
    }

    Style style;
    if (!style.loadFile(stylePath.c_str())) {
        std::fprintf(stderr, "%s\n", style.error().c_str());
        return 1;
    }
    for (const auto& kv : colors) {
        Color c;
        if (!Color::parse(kv.second.c_str(), c)) {
            std::fprintf(stderr, "bad colour %s\n", kv.second.c_str());
            return 2;
        }
        style.setColor(kv.first, c);
    }

    std::unique_ptr<TileSource> source;
    if (!mbtiles.empty()) {
        auto* mb = new MBTilesSource();
        source.reset(mb);
        if (!mb->open(mbtiles.c_str())) {
            std::fprintf(stderr, "cannot open %s%s\n", mbtiles.c_str(),
                MBTilesSource::available() ? "" : " (built without sqlite3)");
            return 1;
        }
    } else {
        source.reset(new DirectorySource(pattern));
    }

    Camera cam;
    cam.setCenterLonLat(lon, lat);
    cam.zoom = zoom;
    cam.bearing = bearing;
    cam.viewW = w;
    cam.viewH = h;
    cam.anchor = { w / 2, h / 2 };

    MapRenderer renderer(*source, style);
    SvgCanvas canvas("Noto Sans CJK SC, sans-serif");
    renderer.render(canvas, cam);
    if (!canvas.save(out.c_str())) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    const RenderStats& s = renderer.stats();
    std::printf("%s: z%d, %d tiles (%d missing), %d features, %d vertices, %d draw calls, %d labels\n", out.c_str(),
        s.tileZoom, s.tilesDrawn, s.tilesMissing, s.features, s.vertices, s.drawCalls, s.labelsPlaced);
    return 0;
}
