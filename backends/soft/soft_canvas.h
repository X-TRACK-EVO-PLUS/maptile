/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_SOFT_CANVAS_H
#define MAPTILE_SOFT_CANVAS_H
#include "maptile/canvas.h"
#include <cstdint>
#include <vector>
namespace MapTile {
/*
 * Draws onto a block of 32-bit pixels (0xAARRGGBB, alpha not touched) with a rasteriser of its own, made for what a vector
 * map is: polygons and polylines of one colour each, no curves, no gradients. It keeps nothing outside the object and calls
 * no library that does, so any number of them can draw at once on different threads.
 *
 * A path becomes coverage by adding up, for each edge, the area it leaves in each pixel of its row (the way font rasterisers
 * do); the running sum along the row is the pixel's coverage. A row is cut in blocks of 16 pixels, and only the blocks an
 * edge has touched are looked at one by one: between them the coverage is that of the run, a span of one colour, which is
 * filled (or skipped) as a whole. So what a shape costs is its edges and its outline, not its area.
 *
 * A line is turned into polygons first: a rectangle for each segment, and a disc at each sharp turn and each round end,
 * all wound the same way, so that where they overlap the coverage stays at one.
 *
 * Text is not drawn: a map that keeps its tiles as bitmaps lays its labels over them, with another canvas.
 */
class SoftCanvas : public Canvas {
public:
    /* The widest picture: a row is 31 blocks of 16 pixels, less two */
    static constexpr int MAX_WIDTH = 480;
    /* `pixels` is `height` rows of `strideBytes`, `width` pixels of each used (at most MAX_WIDTH) */
    SoftCanvas(uint32_t* pixels, int width, int height, int strideBytes);
    void beginFrame(float width, float height, const Color& background) override;
    void fill(const PathBatch& rings, const Color& color) override;
    void stroke(const PathBatch& paths, const LineStyle& style) override;
    Vec2 measureText(const char*, const TextStyle&) override { return { 0, 0 }; }
    void drawText(Vec2, const char*, const TextStyle&) override { }
    void endFrame() override { }

private:
    void edge(float x0, float y0, float x1, float y1);
    void polygon(const Vec2* p, int n);
    void quad(Vec2 a, Vec2 b, float halfWidth);
    void disc(Vec2 c, float r);
    void strokeRun(const Vec2* p, int n, bool closed, const LineStyle& s);
    void resolve(const Color& color);
    uint32_t* _pixels;
    int _w, _h, _stride; /* the picture; _w and _h are those of the frame once it began */
    int _bufW, _bufH;
    std::vector<float> _acc; /* _accStride floats a row */
    int _accStride = 0;
    std::vector<uint32_t> _mask; /* a bit for each block of 16 columns of the row that an edge has touched */
    int _minRow, _maxRow; /* the rows with something since the last resolve */
    std::vector<Vec2> _tmp;
};
} // namespace MapTile
#endif
