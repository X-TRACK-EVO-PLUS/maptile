/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_PIXEL_BLIT_H
#define MAPTILE_PIXEL_BLIT_H
#include <cstdint>
namespace MapTile {
/*
 * Putting the bitmaps of tiles (0xFFRRGGBB, opaque) on a picture of the same format: copied where they are not turned or
 * scaled, sampled bilinearly where they are. What a map made of kept bitmaps does every frame, so it is done here and not
 * by a general image transform that goes through a buffer of its own (LVGL's: about 1.5 times as long on a Cortex-A7).
 * The four pixels of a sample are blended in two words, red and blue in one and green in the other; NEON was slower
 * there, its loads of scattered pixels and the moves between its registers and the core's costing more than it saves.
 * No locks, no allocation.
 */
struct PixelTarget {
    uint32_t* pixels = nullptr;
    int width = 0, height = 0;
    int stride = 0; /* bytes */
};
/* A rectangle of pixels, x0 <= x < x1, y0 <= y < y1 */
struct PixelRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
    PixelRect intersect(const PixelRect& o) const;
};
/* Where a bitmap goes: its pixel (u, v) (the corner of the pixel, not its middle) lands on the picture at
 * (ox + ux * u + vx * v, oy + uy * u + vy * v) */
struct PixelAffine {
    float ox = 0, oy = 0;
    float ux = 1, uy = 0;
    float vx = 0, vy = 1;
};
/* The bitmap `src` (w x h, `stride` bytes a row) on `dst`, inside `clip`. False if the map is degenerate (nothing drawn). */
bool blitAffine(const PixelTarget& dst, const PixelRect& clip, const uint32_t* src, int w, int h, int stride, const PixelAffine& m);
void fillRect(const PixelTarget& dst, const PixelRect& rect, uint32_t color);
/* A line one pixel wide from (x0, y0) to (x1, y1), inside `clip` */
void drawLine(const PixelTarget& dst, const PixelRect& clip, float x0, float y0, float x1, float y1, uint32_t color);
} // namespace MapTile
#endif
