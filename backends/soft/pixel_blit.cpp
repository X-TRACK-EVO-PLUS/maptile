/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "pixel_blit.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace MapTile {
namespace {
    /* a + (b - a) * f / 256 for the three colour bytes at once (red and blue in one word, green in another); opaque */
    inline uint32_t lerp(uint32_t a, uint32_t b, uint32_t f)
    {
        const uint32_t g = 256 - f;
        const uint32_t rb = (((a & 0xFF00FFu) * g + (b & 0xFF00FFu) * f) >> 8) & 0xFF00FFu;
        const uint32_t gr = (((a & 0x00FF00u) * g + (b & 0x00FF00u) * f) >> 8) & 0x00FF00u;
        return 0xFF000000u | rb | gr;
    }
    inline uint32_t* row(const PixelTarget& t, int y) { return reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(t.pixels) + (size_t)y * (size_t)t.stride); }
    /* Narrows [x0, x1] to where a + b x is in [lo, hi] */
    void narrow(float a, float b, float lo, float hi, float& x0, float& x1)
    {
        if (std::fabs(b) < 1e-9f) {
            if (a < lo || a > hi) {
                x1 = x0 - 1;
            }
            return;
        }
        float t0 = (lo - a) / b, t1 = (hi - a) / b;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        x0 = std::max(x0, t0);
        x1 = std::min(x1, t1);
    }
    bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
} // namespace
PixelRect PixelRect::intersect(const PixelRect& o) const
{
    PixelRect r;
    r.x0 = std::max(x0, o.x0);
    r.y0 = std::max(y0, o.y0);
    r.x1 = std::min(x1, o.x1);
    r.y1 = std::min(y1, o.y1);
    return r;
}
bool blitAffine(const PixelTarget& dst, const PixelRect& clipIn, const uint32_t* src, int w, int h, int stride, const PixelAffine& m)
{
    const PixelRect clip = clipIn.intersect({ 0, 0, dst.width, dst.height });
    const float det = m.ux * m.vy - m.vx * m.uy;
    if (!dst.pixels || !src || w <= 0 || h <= 0 || !(std::fabs(det) > 1e-9f)) {
        return false;
    }
    if (clip.empty()) {
        return true;
    }
    const int stridePx = stride / 4;
    /* not turned, not scaled, at a whole pixel: rows copied */
    const float rx = std::round(m.ox), ry = std::round(m.oy);
    if (near(m.ux, 1) && near(m.uy, 0) && near(m.vx, 0) && near(m.vy, 1) && std::fabs(m.ox - rx) < 1e-3f && std::fabs(m.oy - ry) < 1e-3f) {
        const int ox = (int)rx, oy = (int)ry;
        const PixelRect r = clip.intersect({ ox, oy, ox + w, oy + h });
        for (int y = r.y0; y < r.y1; y++) {
            std::memcpy(row(dst, y) + r.x0, src + (size_t)(y - oy) * stridePx + (r.x0 - ox), (size_t)(r.x1 - r.x0) * 4);
        }
        return true;
    }
    /* the bitmap's sample (its pixels' middles at k + 0.5) under the middle of each pixel of the picture:
     * s = inverse(m)(x + 0.5, y + 0.5) - 0.5, linear in x along a row */
    const float bu = m.vy / det, bv = -m.uy / det;
    const float lastU = (float)(w - 1), lastV = (float)(h - 1);
    const int32_t su = (int32_t)std::lround(bu * 65536.0f), sv = (int32_t)std::lround(bv * 65536.0f);
    const int32_t maxU = (w - 1) << 16, maxV = (h - 1) << 16;
    /*
     * In blocks of BLOCK x BLOCK pixels of the picture: a row of a turned bitmap read from end to end walks across its rows,
     * a cache line a pixel when it is turned by a quarter; a block reads a patch of it that stays in the cache.
     */
    constexpr int BLOCK = 16;
    struct Span {
        int x0, x1;
        float au, av;
    };
    Span spans[BLOCK];
    for (int by = clip.y0; by < clip.y1; by += BLOCK) {
        const int rows = std::min(BLOCK, clip.y1 - by);
        int minX = clip.x1, maxX = clip.x0 - 1;
        for (int r = 0; r < rows; r++) {
            const float dy = (float)(by + r) + 0.5f - m.oy, dx0 = 0.5f - m.ox;
            Span& s = spans[r];
            s.au = (dx0 * m.vy - dy * m.vx) / det - 0.5f;
            s.av = (dy * m.ux - dx0 * m.uy) / det - 0.5f;
            /* the pixels whose middle is on the bitmap, its outer half pixel included (sampled at the edge) */
            float x0 = (float)clip.x0, x1 = (float)(clip.x1 - 1);
            narrow(s.au, bu, -0.5f, lastU + 0.5f, x0, x1);
            narrow(s.av, bv, -0.5f, lastV + 0.5f, x0, x1);
            s.x0 = x0 > x1 ? 1 : std::max(clip.x0, (int)std::ceil(x0 - 1e-4f));
            s.x1 = x0 > x1 ? 0 : std::min(clip.x1 - 1, (int)std::floor(x1 + 1e-4f));
            if (s.x0 <= s.x1) {
                minX = std::min(minX, s.x0);
                maxX = std::max(maxX, s.x1);
            }
        }
        for (int bx = minX; bx <= maxX; bx += BLOCK) {
            const int ex = std::min(bx + BLOCK - 1, maxX);
            for (int r = 0; r < rows; r++) {
                const Span& s = spans[r];
                const int x0 = std::max(s.x0, bx), x1 = std::min(s.x1, ex);
                if (x0 > x1) {
                    continue;
                }
                int32_t fu = (int32_t)std::lround((s.au + bu * (float)x0) * 65536.0f);
                int32_t fv = (int32_t)std::lround((s.av + bv * (float)x0) * 65536.0f);
                uint32_t* out = row(dst, by + r) + x0;
                int x = x0;
                /* the first and the last pixels of the span may sample the bitmap's outer half pixel: clamped, one by one */
                auto edge = [&]() {
                    const int32_t cu = std::min(std::max(fu, 0), maxU), cv = std::min(std::max(fv, 0), maxV);
                    const int iu = cu >> 16, iv = cv >> 16;
                    const uint32_t fx = ((uint32_t)cu >> 8) & 0xFF, fy = ((uint32_t)cv >> 8) & 0xFF;
                    const uint32_t* r0 = src + (size_t)iv * stridePx + iu;
                    const uint32_t* r1 = iv + 1 < h ? r0 + stridePx : r0;
                    const int nx = iu + 1 < w ? 1 : 0;
                    *out++ = lerp(lerp(r0[0], r0[nx], fx), lerp(r1[0], r1[nx], fx), fy);
                    x++;
                    fu += su;
                    fv += sv;
                };
                auto inside = [&](int32_t u, int32_t v) { return u >= 0 && v >= 0 && u < maxU && v < maxV; };
                while (x <= x1 && !inside(fu, fv)) {
                    edge();
                }
                /* the run that is inside: as far as both of its ends are (the samples move in a straight line) */
                int last = x1;
                while (last >= x && !inside(fu + su * (last - x), fv + sv * (last - x))) {
                    last--;
                }
                for (; x <= last; x++, fu += su, fv += sv) {
                    const uint32_t* r0 = src + (size_t)(fv >> 16) * stridePx + (fu >> 16);
                    const uint32_t fx = ((uint32_t)fu >> 8) & 0xFF, fy = ((uint32_t)fv >> 8) & 0xFF;
                    /* the four weights, out of 256 */
                    const uint32_t w11 = (fx * fy + 128) >> 8, w10 = fx - w11, w01 = fy - w11, w00 = 256 - fx - fy + w11;
                    const uint32_t a = r0[0], b = r0[1], c = r0[stridePx], d = r0[stridePx + 1];
                    const uint32_t rb = ((a & 0xFF00FFu) * w00 + (b & 0xFF00FFu) * w10 + (c & 0xFF00FFu) * w01 + (d & 0xFF00FFu) * w11) >> 8;
                    const uint32_t g = ((a & 0x00FF00u) * w00 + (b & 0x00FF00u) * w10 + (c & 0x00FF00u) * w01 + (d & 0x00FF00u) * w11) >> 8;
                    *out++ = 0xFF000000u | (rb & 0xFF00FFu) | (g & 0x00FF00u);
                }
                while (x <= x1) {
                    edge();
                }
            }
        }
    }
    return true;
}
void fillRect(const PixelTarget& dst, const PixelRect& rect, uint32_t color)
{
    const PixelRect r = rect.intersect({ 0, 0, dst.width, dst.height });
    if (!dst.pixels || r.empty()) {
        return;
    }
    for (int y = r.y0; y < r.y1; y++) {
        std::fill_n(row(dst, y) + r.x0, r.x1 - r.x0, color);
    }
}
void drawLine(const PixelTarget& dst, const PixelRect& clipIn, float x0, float y0, float x1, float y1, uint32_t color)
{
    const PixelRect clip = clipIn.intersect({ 0, 0, dst.width, dst.height });
    if (!dst.pixels || clip.empty() || !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) {
        return;
    }
    const float dx = x1 - x0, dy = y1 - y0;
    const int steps = (int)std::ceil(std::max(std::fabs(dx), std::fabs(dy)));
    if (steps > 4096) {
        return; /* far off the picture: not a line for it */
    }
    for (int i = 0; i <= steps; i++) {
        const float t = steps ? (float)i / (float)steps : 0;
        const int x = (int)std::floor(x0 + dx * t), y = (int)std::floor(y0 + dy * t);
        if (x >= clip.x0 && x < clip.x1 && y >= clip.y0 && y < clip.y1) {
            row(dst, y)[x] = color;
        }
    }
}
} // namespace MapTile
