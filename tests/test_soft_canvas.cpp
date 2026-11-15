#include "maptile/renderer.h"
#include "soft_canvas.h"
#include "test_common.h"
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>
using namespace MapTile;
namespace {
constexpr uint32_t BG = 0xFFE6E4DD;
struct Picture {
    int w, h;
    std::vector<uint32_t> px;
    SoftCanvas canvas;
    explicit Picture(int width = 64, int height = 48)
        : w(width)
        , h(height)
        , px((size_t)width * height)
        , canvas(px.data(), width, height, width * 4)
    {
        canvas.beginFrame((float)w, (float)h, Color::rgb(BG));
    }
    uint32_t at(int x, int y) const { return px[(size_t)y * w + x]; }
    int red(int x, int y) const { return (at(x, y) >> 16) & 255; }
};
PathBatch ring(std::vector<Vec2> pts)
{
    PathBatch b;
    b.begin();
    for (Vec2 p : pts) {
        b.add(p);
    }
    b.end(true);
    return b;
}
PathBatch path(std::vector<Vec2> pts)
{
    PathBatch b = ring(pts);
    b.closed[0] = 0;
    return b;
}
} // namespace
TEST_CASE("SoftCanvas: a rectangle is solid inside, the background outside, and its edges are partly covered")
{
    Picture p;
    p.canvas.fill(ring({ { 10.5f, 5 }, { 30, 5 }, { 30, 20 }, { 10.5f, 20 } }), Color::rgb(0x2040C0));
    CHECK(p.at(20, 10) == 0xFF2040C0);
    CHECK(p.at(5, 10) == BG);
    CHECK(p.at(40, 10) == BG);
    CHECK(p.at(20, 2) == BG);
    CHECK(p.at(20, 22) == BG);
    /* x 10.5: half of the column 10 is covered, so the colour is half way */
    const int bgRed = (BG >> 16) & 255, fgRed = 0x20;
    CHECK(p.red(10, 10) == doctest::Approx((bgRed + fgRed) / 2.0f).epsilon(0.03));
    /* the edges at whole pixels: no spill */
    CHECK(p.at(30, 10) == BG);
    CHECK(p.at(29, 10) == 0xFF2040C0);
    CHECK(p.at(20, 5) == 0xFF2040C0);
    CHECK(p.at(20, 4) == BG);
}
TEST_CASE("SoftCanvas: the winding decides a hole, and the winding of a ring does not matter by itself")
{
    Picture p;
    PathBatch b;
    b.begin();
    for (Vec2 v : { Vec2 { 5, 5 }, { 55, 5 }, { 55, 40 }, { 5, 40 } }) {
        b.add(v);
    }
    b.end(true);
    b.begin();
    for (Vec2 v : { Vec2 { 20, 15 }, { 20, 30 }, { 40, 30 }, { 40, 15 } }) { /* the other way round */
        b.add(v);
    }
    b.end(true);
    p.canvas.fill(b, Color::rgb(0x00AA00));
    CHECK(p.at(10, 10) == 0xFF00AA00);
    CHECK(p.at(30, 22) == BG);
    CHECK(p.at(45, 35) == 0xFF00AA00);
    /* the same ring wound the other way is as solid */
    Picture q;
    q.canvas.fill(ring({ { 5, 5 }, { 5, 40 }, { 55, 40 }, { 55, 5 } }), Color::rgb(0x00AA00));
    CHECK(q.at(30, 22) == 0xFF00AA00);
}
TEST_CASE("SoftCanvas: the coverage of a triangle adds up to its area")
{
    Picture p(64, 64);
    p.canvas.fill(ring({ { 4.3f, 3.7f }, { 58.2f, 20.1f }, { 17.9f, 59.4f } }), Color::rgb(0x000000));
    double covered = 0;
    const double bg = (BG >> 16) & 255;
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            covered += (bg - p.red(x, y)) / bg;
        }
    }
    const double area = 0.5 * std::fabs((58.2 - 4.3) * (59.4 - 3.7) - (17.9 - 4.3) * (20.1 - 3.7));
    CHECK(covered == doctest::Approx(area).epsilon(0.01));
}
TEST_CASE("SoftCanvas: a shape beyond the picture is cut, and one that goes on past its edge fills to it")
{
    Picture p;
    /* far outside on every side but covering the whole picture */
    p.canvas.fill(ring({ { -500, -500 }, { 900, -500 }, { 900, 900 }, { -500, 900 } }), Color::rgb(0xAA0000));
    for (int y = 0; y < p.h; y += 7) {
        for (int x = 0; x < p.w; x += 5) {
            REQUIRE(p.at(x, y) == 0xFFAA0000);
        }
    }
    /* partly out on the left and right: the middle is covered, the rest is not */
    Picture q;
    q.canvas.fill(ring({ { -30, 10 }, { 100, 10 }, { 100, 20 }, { -30, 20 } }), Color::rgb(0x0000AA));
    CHECK(q.at(0, 15) == 0xFF0000AA);
    CHECK(q.at(63, 15) == 0xFF0000AA);
    CHECK(q.at(30, 9) == BG);
    CHECK(q.at(30, 20) == BG);
    /* entirely outside: nothing, and no crash */
    Picture r;
    r.canvas.fill(ring({ { 200, 200 }, { 300, 200 }, { 300, 300 } }), Color::rgb(0x0000AA));
    r.canvas.fill(ring({ { -200, -200 }, { -100, -200 }, { -100, -100 } }), Color::rgb(0x0000AA));
    CHECK(r.at(10, 10) == BG);
    CHECK(r.at(63, 47) == BG);
}
TEST_CASE("SoftCanvas: a line has the width asked for, its caps and its dashes")
{
    LineStyle s;
    s.color = Color::rgb(0x000000);
    s.width = 4;
    s.cap = Cap::BUTT;
    Picture p;
    p.canvas.stroke(path({ { 10, 20 }, { 50, 20 } }), s);
    for (int y = 18; y < 22; y++) {
        CHECK(p.red(30, y) == 0);
    }
    CHECK(p.at(30, 17) == BG);
    CHECK(p.at(30, 22) == BG);
    CHECK(p.at(8, 20) == BG); /* butt: none beyond the end */
    CHECK(p.at(52, 20) == BG);
    /* round and square caps reach past the ends */
    Picture r;
    s.cap = Cap::ROUND;
    r.canvas.stroke(path({ { 10, 20 }, { 50, 20 } }), s);
    CHECK(r.red(9, 20) < 60);
    CHECK(r.at(7, 20) == BG);
    Picture q;
    s.cap = Cap::SQUARE;
    q.canvas.stroke(path({ { 10, 20 }, { 50, 20 } }), s);
    CHECK(q.red(8, 19) == 0);
    CHECK(q.at(7, 20) == BG);
    /* a line of half a pixel is a pale line, not nothing */
    Picture t;
    s.width = 0.5f;
    t.canvas.stroke(path({ { 10, 20.5f }, { 50, 20.5f } }), s);
    const int pale = t.red(30, 20);
    CHECK(pale > 100);
    CHECK(pale < ((BG >> 16) & 255) - 60);
    /* dashes: on 6, off 4 */
    Picture d;
    const float dash[2] = { 6, 4 };
    LineStyle ds;
    ds.color = Color::rgb(0x000000);
    ds.width = 2;
    ds.cap = Cap::BUTT;
    ds.dash = dash;
    ds.dashCount = 2;
    d.canvas.stroke(path({ { 4, 20 }, { 54, 20 } }), ds);
    int on = 0, off = 0;
    for (int x = 4; x < 54; x++) {
        (d.red(x, 20) < 80 ? on : off)++;
    }
    CHECK(on == doctest::Approx(30).epsilon(0.15));
    CHECK(off == doctest::Approx(20).epsilon(0.2));
    CHECK(d.red(6, 20) == 0);
    CHECK(d.red(11, 20) > 200); /* in the gap */
}
TEST_CASE("SoftCanvas: lines that cross, and a corner, do not darken or leave a notch, and a translucent colour mixes")
{
    LineStyle s;
    s.color = Color::rgb(0x000000);
    s.width = 6;
    s.cap = Cap::BUTT;
    s.join = Join::ROUND;
    Picture p;
    p.canvas.stroke(path({ { 10, 10 }, { 40, 10 }, { 40, 40 } }), s);
    CHECK(p.red(25, 10) == 0);
    CHECK(p.red(40, 25) == 0);
    /* the outer corner is round: the very corner is not covered, the middle of the bend is */
    CHECK(p.red(40, 10) == 0);
    CHECK(p.red(43, 7) > 100);
    /* a stroke over itself stays as solid as one */
    Picture q;
    PathBatch b = path({ { 10, 10 }, { 50, 10 } });
    b.begin();
    b.add({ 10, 11 });
    b.add({ 50, 11 });
    b.end(false);
    q.canvas.stroke(b, s);
    CHECK(q.red(30, 10) == 0);
    /* alpha: half of black over the background is half the background */
    Picture t;
    Color half = Color::rgb(0x000000);
    half.a = 128;
    t.canvas.fill(ring({ { 5, 5 }, { 30, 5 }, { 30, 30 }, { 5, 30 } }), half);
    CHECK(t.red(15, 15) == doctest::Approx(((BG >> 16) & 255) * 0.5f).epsilon(0.02));
}
TEST_CASE("SoftCanvas: pictures on different threads are independent")
{
    auto draw = [](uint32_t colour, uint32_t* out) {
        Picture p;
        for (int i = 0; i < 200; i++) {
            p.canvas.fill(ring({ { 5, 5.0f + (i % 7) }, { 50, 5 }, { 50, 30 }, { 5, 30 } }), Color::rgb(colour));
        }
        *out = p.at(25, 20);
    };
    uint32_t a = 0, b = 0, c = 0;
    std::thread t1(draw, 0x112233u, &a), t2(draw, 0x445566u, &b), t3(draw, 0x778899u, &c);
    t1.join();
    t2.join();
    t3.join();
    CHECK(a == 0xFF112233);
    CHECK(b == 0xFF445566);
    CHECK(c == 0xFF778899);
}
TEST_CASE("SoftCanvas: a tile of the test data is drawn, with the colours of the style")
{
    MBTilesSource source;
    Style style;
    if (!MBTilesSource::available() || !source.open("tests/data/beijing-mini.mbtiles") || !style.loadFile("tests/data/style-light.json")) {
        MESSAGE("fixture needs sqlite3, skipping");
        return;
    }
    MapRenderer r(source, style);
    const WorldPos w = lonLatToWorld(116.3895, 39.9225);
    const int x = (int)(w.x * (1 << 15)), y = (int)(w.y * (1 << 15));
    const int size = 256 + 4;
    Picture p(size, size);
    RenderOptions opt;
    opt.labels = false;
    REQUIRE(r.drawBitmapTile(p.canvas, 15, x, y, 15, 2, opt));
    int other = 0, roadish = 0;
    for (uint32_t v : p.px) {
        other += v != 0xFFF2EFE9u && v != BG;
        roadish += (v & 0xFFFFFF) > 0xF0F0F0;
    }
    CHECK(other > size * size / 20);
    CHECK(roadish > 100);
    /* the same tile twice is the same picture */
    Picture q(size, size);
    REQUIRE(r.drawBitmapTile(q.canvas, 15, x, y, 15, 2, opt));
    CHECK(q.px == p.px);
}
#include "bitmap_pool.h"
#include <atomic>
#include <chrono>
namespace {
struct PoolFixture {
    MBTilesSource source;
    Style style;
    bool ok = false;
    PoolFixture() { ok = MBTilesSource::available() && source.open("tests/data/beijing-mini.mbtiles") && style.loadFile("tests/data/style-light.json"); }
    std::vector<BitmapPool::Job> jobs(int level, int n, float zoom) const
    {
        const WorldPos w = lonLatToWorld(116.3895, 39.9225);
        const int x = (int)(w.x * (1 << level)), y = (int)(w.y * (1 << level));
        std::vector<BitmapPool::Job> out;
        for (int i = 0; i < n; i++) {
            out.push_back({ level, x + (i % 3) - 1, y + (i / 3) - 1, zoom, 2 });
        }
        return out;
    }
};
bool waitFor(BitmapPool& pool, int ms = 8000)
{
    for (int i = 0; i < ms && pool.busy(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return !pool.busy();
}
} // namespace
TEST_CASE("BitmapPool: with no threads the tiles are drawn at once, and are what drawing them directly gives")
{
    PoolFixture fx;
    if (!fx.ok) {
        MESSAGE("fixture needs sqlite3, skipping");
        return;
    }
    TileStore store(fx.source);
    BitmapPool pool(store, 0);
    pool.want({}); /* no style yet: nothing */
    pool.setStyle(fx.style);
    const auto jobs = fx.jobs(15, 4, 15.3f);
    pool.want(jobs);
    CHECK_FALSE(pool.busy());
    std::vector<TileBitmap> got = pool.take();
    REQUIRE(got.size() == 4);
    CHECK(pool.take().empty());
    MapRenderer direct(store, fx.style);
    for (size_t i = 0; i < got.size(); i++) {
        TileBitmap b;
        b.level = jobs[i].level;
        b.x = jobs[i].x;
        b.y = jobs[i].y;
        b.zoom = jobs[i].zoom;
        b.bleed = jobs[i].bleed;
        REQUIRE(drawTileBitmap(direct, b));
        CHECK(got[i].size == b.size);
        CHECK(got[i].pixels == doctest::Approx(256 * std::pow(2.0f, 0.3f)));
        CHECK(std::memcmp(got[i].data.get(), b.data.get(), (size_t)b.size * b.size * 4) == 0);
    }
    /* out of range: nothing comes of it */
    pool.want({ { 30, 0, 0, 14, 0 } });
    CHECK(pool.take().empty());
}
TEST_CASE("BitmapPool: threads draw the same tiles, asking again drops what was not started, a new style drops them all")
{
    PoolFixture fx;
    if (!fx.ok) {
        MESSAGE("fixture needs sqlite3, skipping");
        return;
    }
    TileStore store(fx.source);
    BitmapPool inline0(store, 0);
    inline0.setStyle(fx.style);
    const auto jobs = fx.jobs(16, 9, 16);
    inline0.want(jobs);
    std::vector<TileBitmap> want = inline0.take();
    REQUIRE(want.size() == 9);
    BitmapPool pool(store, 3);
    CHECK(pool.threads() == 3);
    pool.setStyle(fx.style);
    pool.want(jobs);
    REQUIRE(waitFor(pool));
    std::vector<TileBitmap> got = pool.take();
    REQUIRE(got.size() == 9);
    for (const TileBitmap& g : got) {
        bool found = false;
        for (const TileBitmap& w : want) {
            if (w.x == g.x && w.y == g.y) {
                found = true;
                CHECK(std::memcmp(g.data.get(), w.data.get(), (size_t)w.size * w.size * 4) == 0);
            }
        }
        CHECK(found);
    }
    /* one thread, a long list, and then a new one: the first list is not all drawn */
    BitmapPool one(store, 1);
    one.setStyle(fx.style);
    std::vector<BitmapPool::Job> many;
    for (int i = 0; i < 60; i++) {
        many.push_back({ 16, 100000 + i, 50000, 16, 2 }); /* tiles the data has not: quick, but there are many */
    }
    one.want(many);
    one.want(fx.jobs(16, 2, 16));
    REQUIRE(waitFor(one));
    CHECK(one.take().size() <= 2 + 3);
    /* a new style: what was done is gone */
    BitmapPool styled(store, 2);
    styled.setStyle(fx.style);
    styled.want(jobs);
    REQUIRE(waitFor(styled));
    styled.setStyle(fx.style);
    CHECK(styled.take().empty());
}
TEST_CASE("TileStore: threads that want one tile decode it once, and everyone gets it")
{
    PoolFixture fx;
    if (!fx.ok) {
        MESSAGE("fixture needs sqlite3, skipping");
        return;
    }
    TileStore store(fx.source);
    const WorldPos w = lonLatToWorld(116.3895, 39.9225);
    const int x = (int)(w.x * (1 << 14)), y = (int)(w.y * (1 << 14));
    std::atomic<int> decoded { 0 }, have { 0 };
    std::vector<std::thread> threads;
    for (int i = 0; i < 6; i++) {
        threads.emplace_back([&] {
            bool d = false;
            auto t = store.get(14, x, y, &d);
            decoded += d;
            have += t != nullptr;
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    CHECK(decoded == 1);
    CHECK(have == 6);
    CHECK(store.cached(14, x, y));
    CHECK_FALSE(store.cached(14, x + 1000, y));
    store.setLimit(0);
    CHECK(store.size() <= 1);
    store.clear();
    CHECK(store.size() == 0);
}
