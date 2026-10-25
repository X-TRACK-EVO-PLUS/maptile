#include "maptile/renderer.h"
#include "svg_canvas.h"
#include "test_common.h"
#include <algorithm>
#include <cstring>
#include <memory>

using namespace MapTile;

namespace {

struct Fixture {
    MBTilesSource source;
    Style style;
    bool ok = false;

    Fixture()
    {
        ok = MBTilesSource::available() && source.open("tests/data/beijing-mini.mbtiles")
            && style.loadFile("tests/data/style-light.json");
    }
    Camera camera(float zoom = 16, float bearing = 0) const
    {
        Camera c;
        c.setCenterLonLat(116.3895, 39.9225);
        c.zoom = zoom;
        c.bearing = bearing;
        c.viewW = 240;
        c.viewH = 320;
        c.anchor = { 120, 160 };
        return c;
    }
};

/* Records what the renderer asks for */
class Recorder : public Canvas {
public:
    int fills = 0, strokes = 0, texts = 0, frames = 0;
    size_t vertices = 0;
    float width = 0, height = 0;
    std::vector<std::string> words;
    std::vector<Vec2> where;
    std::vector<float> lineWidths;

    void beginFrame(float w, float h, const Color&) override
    {
        frames++;
        width = w;
        height = h;
    }
    void fill(const PathBatch& b, const Color&) override
    {
        fills++;
        vertices += b.pts.size();
    }
    void stroke(const PathBatch& b, const LineStyle& s) override
    {
        strokes++;
        vertices += b.pts.size();
        lineWidths.push_back(s.width);
    }
    Vec2 measureText(const char* t, const TextStyle& s) override { return { (float)strlen(t) * s.size * 0.4f, s.size * 1.2f }; }
    void drawText(Vec2 c, const char* t, const TextStyle&) override
    {
        texts++;
        words.push_back(t);
        where.push_back(c);
    }
    void endFrame() override { }
};

} // namespace

TEST_CASE("MapRenderer: draws the city centre")
{
    Fixture fx;
    if (!fx.ok) {
        MESSAGE("fixture needs sqlite3, skipping");
        return;
    }
    MapRenderer r(fx.source, fx.style);
    Recorder rec;
    r.render(rec, fx.camera());
    const RenderStats& s = r.stats();
    CHECK(rec.frames == 1);
    CHECK(rec.width == 240);
    CHECK(s.tileZoom == 14);
    CHECK(s.tilesDrawn >= 1);
    CHECK(s.tilesMissing == 0);
    CHECK(s.features > 100);
    CHECK(rec.fills > 0);
    CHECK(rec.strokes > 0);
    CHECK(rec.texts > 0);
    CHECK(rec.texts == s.labelsPlaced);
    /* one call per style layer, however many tiles and features */
    CHECK(s.drawCalls <= (int)fx.style.layers().size());
}

TEST_CASE("MapRenderer: line widths follow the zoom")
{
    Fixture fx;
    if (!fx.ok)
        return;
    MapRenderer r(fx.source, fx.style);
    Recorder a, b;
    r.render(a, fx.camera(15));
    r.render(b, fx.camera(17));
    REQUIRE_FALSE(a.lineWidths.empty());
    REQUIRE_FALSE(b.lineWidths.empty());
    float maxA = *std::max_element(a.lineWidths.begin(), a.lineWidths.end());
    float maxB = *std::max_element(b.lineWidths.begin(), b.lineWidths.end());
    CHECK(maxB > maxA * 1.5f);
}

TEST_CASE("MapRenderer: zoom is continuous, tiles come from the nearest level")
{
    Fixture fx;
    if (!fx.ok)
        return;
    MapRenderer r(fx.source, fx.style);
    CHECK(r.tileLevel(14.4f) == 14);
    CHECK(r.tileLevel(13.6f) == 14);
    CHECK(r.tileLevel(13.4f) == 13);
    CHECK(r.tileLevel(19.0f) == 14); /* over-zoom reuses the highest level */
    CHECK(r.tileLevel(3.0f) == 13);
    Recorder rec;
    r.render(rec, fx.camera(18.3f)); /* four levels beyond the data */
    CHECK(r.stats().tileZoom == 14);
    CHECK(r.stats().features > 0);
}

TEST_CASE("MapRenderer: rotation keeps the picture complete")
{
    Fixture fx;
    if (!fx.ok)
        return;
    MapRenderer r(fx.source, fx.style);
    Recorder north, rotated;
    r.render(north, fx.camera(16, 0));
    int featuresNorth = r.stats().features;
    r.render(rotated, fx.camera(16, 90));
    /* the same area is visible, only turned: counts stay in the same range */
    CHECK(r.stats().features > featuresNorth / 3);
    CHECK(r.stats().features < featuresNorth * 3);
    CHECK(rotated.fills > 0);
}

TEST_CASE("MapRenderer: places outside the data are reported missing")
{
    Fixture fx;
    if (!fx.ok)
        return;
    MapRenderer r(fx.source, fx.style);
    Camera c = fx.camera(16);
    c.setCenterLonLat(121.47, 31.23); /* Shanghai */
    Recorder rec;
    r.render(rec, c);
    CHECK(r.stats().tilesDrawn == 0);
    CHECK(r.stats().tilesMissing > 0);
    CHECK(rec.frames == 1);
}

TEST_CASE("MapRenderer: load budget defers decoding to later frames")
{
    Fixture fx;
    if (!fx.ok)
        return;
    MapRenderer r(fx.source, fx.style);
    RenderOptions opt;
    opt.loadBudget = 1;
    Recorder rec;
    r.render(rec, fx.camera(15), opt);
    int pending = r.stats().tilesPending;
    CHECK(pending > 0);
    int rounds = 0;
    while (r.stats().tilesPending > 0 && rounds++ < 20)
        r.render(rec, fx.camera(15), opt);
    CHECK(r.stats().tilesPending == 0);
    CHECK(r.stats().tilesDrawn > 1);
}

TEST_CASE("MapRenderer: labels keep out of reserved rectangles and never collide")
{
    Fixture fx;
    if (!fx.ok)
        return;
    MapRenderer r(fx.source, fx.style);
    RenderOptions opt;
    opt.labelKeepOut.push_back({ 0, 0, 240, 160 }); /* the whole top half */
    Recorder rec;
    r.render(rec, fx.camera(15), opt);
    for (const Vec2& p : rec.where)
        CHECK(p.y > 150);
    Recorder free;
    r.render(free, fx.camera(15));
    CHECK(free.texts > rec.texts);
    CHECK(rec.texts > 0);
}

TEST_CASE("MapRenderer: palette change re-themes the next frame")
{
    Fixture fx;
    if (!fx.ok)
        return;
    MapRenderer r(fx.source, fx.style);
    SvgCanvas a, b;
    r.render(a, fx.camera());
    fx.style.setColor("water", Color::rgb(0xff00ff));
    r.render(b, fx.camera());
    CHECK(a.svg().find("#ff00ff") == std::string::npos);
    CHECK(b.svg().find("#ff00ff") != std::string::npos);
}

TEST_CASE("SvgCanvas: well-formed output")
{
    Fixture fx;
    if (!fx.ok)
        return;
    MapRenderer r(fx.source, fx.style);
    SvgCanvas svg;
    r.render(svg, fx.camera());
    const std::string& s = svg.svg();
    CHECK(s.compare(0, 4, "<svg") == 0);
    CHECK(s.find("</svg>") == s.size() - 7);
    CHECK(s.find("<path") != std::string::npos);
    CHECK(s.find("<text") != std::string::npos);
    CHECK(s.size() > 5000);
}
