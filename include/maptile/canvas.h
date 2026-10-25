/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_CANVAS_H
#define MAPTILE_CANVAS_H
#include "maptile/geom.h"
#include <cstdint>
#include <vector>

namespace MapTile {

enum class Cap { BUTT,
    ROUND,
    SQUARE };
enum class Join { MITER,
    ROUND,
    BEVEL };

/* Many sub-paths in screen pixels, handed to the backend in one call. */
struct PathBatch {
    std::vector<Vec2> pts;
    std::vector<uint32_t> ends; /* index one past the last point of each sub-path */
    std::vector<uint8_t> closed; /* 1 if the sub-path is a ring */

    void clear()
    {
        pts.clear();
        ends.clear();
        closed.clear();
    }
    bool empty() const { return ends.empty(); }
    void begin() { _mark = (uint32_t)pts.size(); }
    void add(Vec2 p) { pts.push_back(p); }
    uint32_t count() const { return (uint32_t)pts.size() - _mark; }
    void end(bool isClosed)
    {
        ends.push_back((uint32_t)pts.size());
        closed.push_back(isClosed ? 1 : 0);
    }
    /* Drop the sub-path started by begin() */
    void rollback() { pts.resize(_mark); }

private:
    uint32_t _mark = 0;
};

struct LineStyle {
    Color color;
    float width = 1;
    Cap cap = Cap::ROUND;
    Join join = Join::ROUND;
    const float* dash = nullptr; /* on, off, on, off ... in pixels */
    int dashCount = 0;
};

struct TextStyle {
    Color color;
    Color halo { 0, 0, 0, 0 }; /* alpha 0: no halo */
    float size = 12; /* pixels */
    int weight = 500; /* CSS-like 100..900; the backend maps it onto its fonts */
};

/*
 * What the renderer draws onto. A backend turns these calls into pixels
 * (LVGL vector draw), markup (SVG, for tests and design tools), or anything
 * else. All coordinates are screen pixels, origin top-left, y down.
 */
class Canvas {
public:
    virtual ~Canvas() = default;
    virtual void beginFrame(float width, float height, const Color& background) = 0;
    /* Fill every ring in `rings` (non-zero winding, so holes need opposite winding) */
    virtual void fill(const PathBatch& rings, const Color& color) = 0;
    virtual void stroke(const PathBatch& paths, const LineStyle& style) = 0;
    virtual Vec2 measureText(const char* utf8, const TextStyle& style) = 0;
    /* Draws `utf8` centred on `center` */
    virtual void drawText(Vec2 center, const char* utf8, const TextStyle& style) = 0;
    virtual void endFrame() = 0;
};

} // namespace MapTile

#endif // MAPTILE_CANVAS_H
