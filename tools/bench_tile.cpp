/*
 * bench_tile: how long a map tile takes to draw with the software rasteriser of maptile, for the speed of one against another.
 *
 *   bench_tile --mbtiles FILE --style FILE [--lon 116.39 --lat 39.91] [--level 16] [--zoom 16] [--n 20] [--out tile.ppm]
 */
#include "maptile/renderer.h"
#include "soft_canvas.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace MapTile;
static double now()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
/* draws nothing: what is left of a tile's time is the projecting and the simplifying of its geometry */
struct NullCanvas : Canvas {
    void beginFrame(float, float, const Color&) override { }
    void fill(const PathBatch&, const Color&) override { }
    void stroke(const PathBatch&, const LineStyle&) override { }
    Vec2 measureText(const char*, const TextStyle&) override { return { 0, 0 }; }
    void drawText(Vec2, const char*, const TextStyle&) override { }
    void endFrame() override { }
};
int main(int argc, char** argv)
{
    std::string mb, style, out;
    double lon = 116.3975, lat = 39.9087;
    int level = 16, n = 20, bleed = 2;
    float zoom = -1;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string a = argv[i];
        const char* v = argv[i + 1];
        if (a == "--mbtiles")
            mb = v;
        else if (a == "--style")
            style = v;
        else if (a == "--lon")
            lon = atof(v);
        else if (a == "--lat")
            lat = atof(v);
        else if (a == "--level")
            level = atoi(v);
        else if (a == "--zoom")
            zoom = (float)atof(v);
        else if (a == "--n")
            n = atoi(v);
        else if (a == "--out")
            out = v;
    }
    if (zoom < 0)
        zoom = (float)level;
    MBTilesSource src;
    Style st;
    if (!src.open(mb.c_str()) || !st.loadFile(style.c_str())) {
        fprintf(stderr, "cannot open the data or the style\n");
        return 1;
    }
    MapRenderer r(src, st);
    const WorldPos w = lonLatToWorld(lon, lat);
    const int x = (int)(w.x * (1 << level)), y = (int)(w.y * (1 << level));
    const int size = (int)std::ceil(MapRenderer::bitmapPixels(level, zoom)) + 2 * bleed;
    std::vector<uint32_t> px((size_t)size * size);
    SoftCanvas canvas(px.data(), size, size, size * 4);
    RenderOptions opt;
    opt.labels = false;
    double t0 = now();
    r.drawBitmapTile(canvas, level, x, y, zoom, bleed, opt); /* the first: decodes the tile */
    const double first = now() - t0;
    double best = 1e9, sum = 0;
    for (int i = 0; i < n; i++) {
        r.resetStats();
        t0 = now();
        r.drawBitmapTile(canvas, level, x, y, zoom, bleed, opt);
        const double t = now() - t0;
        best = std::min(best, t);
        sum += t;
    }
    NullCanvas none;
    double nbest = 1e9;
    for (int i = 0; i < n; i++) {
        t0 = now();
        r.drawBitmapTile(none, level, x, y, zoom, bleed, opt);
        nbest = std::min(nbest, now() - t0);
    }
    printf("geometry alone (no pixels) best %.1f ms\n", nbest);
    printf("tile z%d %d,%d at zoom %.2f, %dx%d px: first %.1f ms (with decode), then best %.1f ms, avg %.1f ms; %d vertices %d features %d calls\n", level, x, y,
        zoom, size, size, first, best, sum / n, r.stats().vertices, r.stats().features, r.stats().drawCalls);
    if (!out.empty()) {
        FILE* f = fopen(out.c_str(), "wb");
        fprintf(f, "P6\n%d %d\n255\n", size, size);
        for (uint32_t p : px) {
            const unsigned char c[3] = { (unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p };
            fwrite(c, 1, 3, f);
        }
        fclose(f);
    }
    return 0;
}
