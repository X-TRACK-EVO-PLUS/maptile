/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "soft_canvas.h"
#include <algorithm>
#include <cmath>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
namespace MapTile {
namespace {
    constexpr float TWO_PI = 6.2831853f;
    /* the next whole number up from one that is not negative (the library's ceil is a call on the CPU of the board) */
    inline int ceilPos(float v)
    {
        const int i = (int)v;
        return (float)i < v ? i + 1 : i;
    }
    /* a colour channel moved towards `src` by k/255, exactly: (dst*(255-k) + src*k) / 255 rounded, with no division */
    inline int mixChannel(int dst, int src, int k)
    {
        const int t = dst * (255 - k) + src * k + 128;
        return (t + (t >> 8)) >> 8;
    }
    inline uint32_t mixPixel(uint32_t d, int sr, int sg, int sb, int k)
    {
        return 0xFF000000u | ((uint32_t)mixChannel((d >> 16) & 255, sr, k) << 16) | ((uint32_t)mixChannel((d >> 8) & 255, sg, k) << 8)
            | (uint32_t)mixChannel(d & 255, sb, k);
    }
} // namespace
SoftCanvas::SoftCanvas(uint32_t* pixels, int width, int height, int strideBytes)
    : _pixels(pixels)
    , _w(std::min(width, MAX_WIDTH))
    , _h(height)
    , _stride(strideBytes / 4)
    , _bufW(std::min(width, MAX_WIDTH))
    , _bufH(height)
{
    _accStride = _bufW + 20;
    _acc.assign((size_t)_accStride * (size_t)std::max(_bufH, 1), 0.0f);
    _mask.assign((size_t)std::max(_bufH, 1), 0u);
    _minRow = 1 << 30;
    _maxRow = -1;
}
void SoftCanvas::beginFrame(float width, float height, const Color& background)
{
    _w = std::min(_bufW, ceilPos(std::max(width, 0.0f)));
    _h = std::min(_bufH, ceilPos(std::max(height, 0.0f)));
    const uint32_t px = 0xFF000000u | ((uint32_t)background.r << 16) | ((uint32_t)background.g << 8) | background.b;
    for (int y = 0; y < _h; y++) {
        uint32_t* row = _pixels + (size_t)y * _stride;
        std::fill(row, row + _w, px);
    }
}
/* One edge, left in the accumulation buffer as the area it covers to its right, row by row */
void SoftCanvas::edge(float x0, float y0, float x1, float y1)
{
    if (y0 == y1) {
        return;
    }
    float dir = 1;
    if (y0 > y1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
        dir = -1;
    }
    if (y1 <= 0 || y0 >= (float)_h) {
        return;
    }
    const float dxdy = (x1 - x0) / (y1 - y0);
    float x = x0;
    if (y0 < 0) {
        x -= y0 * dxdy;
        y0 = 0;
    }
    const float fw = (float)_w;
    const int yStart = (int)y0, yEnd = std::min(_h, ceilPos(y1));
    _minRow = std::min(_minRow, yStart);
    _maxRow = std::max(_maxRow, yEnd);
    for (int y = yStart; y < yEnd; y++) {
        const float dy = std::min((float)(y + 1), y1) - std::max((float)y, y0);
        const float xnext = x + dxdy * dy;
        const float d = dy * dir;
        /* what is left of the picture counts in its first column, what is right of it is not seen */
        float xa = std::min(std::max(x, 0.0f), fw), xb = std::min(std::max(xnext, 0.0f), fw);
        if (xa > xb) {
            std::swap(xa, xb);
        }
        float* a = &_acc[(size_t)y * _accStride];
        const int xai = (int)xa;
        const float xaFloor = (float)xai;
        const int xbi = ceilPos(xb);
        const int lo = xai >> 4, hi = (xbi + 1) >> 4;
        _mask[y] |= ((2u << hi) - 1u) & ~((1u << lo) - 1u);
        if (xbi <= xai + 1) {
            const float xmf = 0.5f * (xa + xb) - xaFloor;
            a[xai] += d - d * xmf;
            a[xai + 1] += d * xmf;
        } else {
            const float s = 1.0f / (xb - xa);
            const float xaf = xa - xaFloor;
            const float a0 = 0.5f * s * (1.0f - xaf) * (1.0f - xaf);
            const float xbf = xb - (float)xbi + 1.0f;
            const float am = 0.5f * s * xbf * xbf;
            a[xai] += d * a0;
            if (xbi == xai + 2) {
                a[xai + 1] += d * (1.0f - a0 - am);
            } else {
                const float a1 = s * (1.5f - xaf);
                a[xai + 1] += d * (a1 - a0);
                for (int xi = xai + 2; xi < xbi - 1; xi++) {
                    a[xi] += d * s;
                }
                const float a2 = a1 + (float)(xbi - xai - 3) * s;
                a[xbi - 1] += d * (1.0f - a2 - am);
            }
            a[xbi] += d * am;
        }
        x = xnext;
    }
}
void SoftCanvas::polygon(const Vec2* p, int n)
{
    if (n < 3) {
        return;
    }
    float lo = p[0].y, hi = p[0].y, left = p[0].x, right = p[0].x;
    for (int i = 1; i < n; i++) {
        lo = std::min(lo, p[i].y);
        hi = std::max(hi, p[i].y);
        left = std::min(left, p[i].x);
        right = std::max(right, p[i].x);
    }
    if (hi <= 0 || lo >= (float)_h || right <= 0 || left >= (float)_w) {
        return;
    }
    for (int i = 0; i < n; i++) {
        const Vec2& a = p[i];
        const Vec2& b = p[i + 1 == n ? 0 : i + 1];
        edge(a.x, a.y, b.x, b.y);
    }
}
/* The coverage of everything added, laid over the pixels in `color`; the buffer is left empty */
void SoftCanvas::resolve(const Color& color)
{
    if (_maxRow < 0) {
        return;
    }
    const int y0 = std::max(0, _minRow), y1 = std::min(_h, _maxRow);
    _minRow = 1 << 30;
    _maxRow = -1;
    const int alpha = color.a;
    const int sr = color.r, sg = color.g, sb = color.b;
    const uint32_t solid = 0xFF000000u | ((uint32_t)sr << 16) | ((uint32_t)sg << 8) | (uint32_t)sb;
    /* a run of pixels with one coverage */
    auto span = [&](uint32_t* row, int x0, int x1, float sum) {
        if (x1 <= x0) {
            return;
        }
        const int k = (int)(std::min(std::fabs(sum), 1.0f) * (float)alpha + 0.5f);
        if (k <= 0) {
            return;
        }
        if (k >= 255) {
            int x = x0;
#if defined(__ARM_NEON)
            const uint32x4_t v = vdupq_n_u32(solid);
            for (; x + 4 <= x1; x += 4) {
                vst1q_u32(row + x, v);
            }
#endif
            for (; x < x1; x++) {
                row[x] = solid;
            }
            return;
        }
        for (int x = x0; x < x1; x++) {
            row[x] = mixPixel(row[x], sr, sg, sb, k);
        }
    };
    for (int y = y0; y < y1; y++) {
        uint32_t m = _mask[y];
        if (!m) {
            continue;
        }
        _mask[y] = 0;
        float* a = &_acc[(size_t)y * _accStride];
        uint32_t* row = _pixels + (size_t)y * _stride;
        float sum = 0;
        int x = 0;
        while (m) {
            const int bx = __builtin_ctz(m) << 4;
            m &= m - 1;
            if (bx > x) {
                span(row, x, std::min(bx, _w), sum); /* the untouched blocks before this one */
                x = bx;
            }
            const int end = std::min(bx + 16, _accStride);
#if defined(__ARM_NEON)
            /* eight pixels at a time: the running sums first (they depend on each other), then the blending of all eight */
            for (; x + 8 <= end && x + 8 <= _w; x += 8) {
                float cov[8];
                for (int i = 0; i < 8; i++) {
                    sum += a[x + i];
                    a[x + i] = 0;
                    cov[i] = sum;
                }
                const float32x4_t one = vdupq_n_f32(1.0f), scale = vdupq_n_f32((float)alpha), half = vdupq_n_f32(0.5f);
                const float32x4_t c0 = vminq_f32(vabsq_f32(vld1q_f32(cov)), one), c1 = vminq_f32(vabsq_f32(vld1q_f32(cov + 4)), one);
                const uint32x4_t k0 = vcvtq_u32_f32(vmlaq_f32(half, c0, scale)), k1 = vcvtq_u32_f32(vmlaq_f32(half, c1, scale));
                const uint8x8_t k = vmovn_u16(vcombine_u16(vmovn_u32(k0), vmovn_u32(k1)));
                const uint8x8_t ik = vmvn_u8(k);
                uint8x8x4_t px = vld4_u8((const uint8_t*)(row + x)); /* b, g, r, a of eight pixels */
                const uint16x8_t round = vdupq_n_u16(128);
                uint16x8_t t = vaddq_u16(vmlal_u8(vmull_u8(px.val[0], ik), vdup_n_u8((uint8_t)sb), k), round);
                px.val[0] = vshrn_n_u16(vaddq_u16(t, vshrq_n_u16(t, 8)), 8);
                t = vaddq_u16(vmlal_u8(vmull_u8(px.val[1], ik), vdup_n_u8((uint8_t)sg), k), round);
                px.val[1] = vshrn_n_u16(vaddq_u16(t, vshrq_n_u16(t, 8)), 8);
                t = vaddq_u16(vmlal_u8(vmull_u8(px.val[2], ik), vdup_n_u8((uint8_t)sr), k), round);
                px.val[2] = vshrn_n_u16(vaddq_u16(t, vshrq_n_u16(t, 8)), 8);
                px.val[3] = vdup_n_u8(255);
                vst4_u8((uint8_t*)(row + x), px);
            }
#endif
            for (; x < end; x++) {
                sum += a[x];
                a[x] = 0;
                if (x < _w) {
                    const int k = (int)(std::min(std::fabs(sum), 1.0f) * (float)alpha + 0.5f);
                    if (k > 0) {
                        row[x] = mixPixel(row[x], sr, sg, sb, k);
                    }
                }
            }
        }
        span(row, x, _w, sum); /* what is right of the last edge: nothing, or the shape that goes on to the picture's edge */
    }
}
void SoftCanvas::fill(const PathBatch& rings, const Color& color)
{
    uint32_t begin = 0;
    for (size_t s = 0; s < rings.ends.size(); s++) {
        polygon(rings.pts.data() + begin, (int)(rings.ends[s] - begin));
        begin = rings.ends[s];
    }
    resolve(color);
}
/* A segment as a rectangle, wound the same way whichever way the segment goes */
void SoftCanvas::quad(Vec2 a, Vec2 b, float h)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) {
        return;
    }
    const float nx = -dy / len * h, ny = dx / len * h;
    const Vec2 p0 = { a.x + nx, a.y + ny }, p1 = { b.x + nx, b.y + ny }, p2 = { b.x - nx, b.y - ny }, p3 = { a.x - nx, a.y - ny };
    const float left = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x)), right = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
    const float top = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y)), bottom = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
    if (bottom <= 0 || top >= (float)_h || right <= 0 || left >= (float)_w) {
        return;
    }
    edge(p0.x, p0.y, p1.x, p1.y);
    edge(p1.x, p1.y, p2.x, p2.y);
    edge(p2.x, p2.y, p3.x, p3.y);
    edge(p3.x, p3.y, p0.x, p0.y);
}
/* A disc wound as quad() is */
void SoftCanvas::disc(Vec2 c, float r)
{
    const int n = r < 1.5f ? 6 : r < 3 ? 8
                                       : 12;
    Vec2 p[12];
    for (int i = 0; i < n; i++) {
        const float t = -TWO_PI * (float)i / (float)n;
        p[i] = { c.x + std::cos(t) * r, c.y + std::sin(t) * r };
    }
    polygon(p, n);
}
void SoftCanvas::strokeRun(const Vec2* p, int n, bool closed, const LineStyle& s)
{
    const float h = s.width * 0.5f;
    const int segs = closed ? n : n - 1;
    /* a corner or an end shows only on a line wide enough for it */
    const bool wide = s.width >= 2.5f;
    for (int i = 0; i < segs; i++) {
        const Vec2& a = p[i];
        const Vec2& b = p[(i + 1) % n];
        /* a segment far outside costs nothing */
        const float pad = h + 1;
        if ((a.x < -pad && b.x < -pad) || (a.y < -pad && b.y < -pad) || (a.x > _w + pad && b.x > _w + pad) || (a.y > _h + pad && b.y > _h + pad)) {
            continue;
        }
        const bool first = i == 0 && !closed, last = i == segs - 1 && !closed;
        if (s.cap == Cap::SQUARE && (first || last)) {
            Vec2 a2 = a, b2 = b;
            const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
            if (len > 1e-6f) {
                if (first) {
                    a2 = { a.x - dx / len * h, a.y - dy / len * h };
                }
                if (last) {
                    b2 = { b.x + dx / len * h, b.y + dy / len * h };
                }
            }
            quad(a2, b2, h);
        } else {
            quad(a, b, h);
        }
        if (!wide) {
            continue;
        }
        if (!last && s.join != Join::BEVEL) {
            /* only where the line turns enough for the rectangles to leave a notch */
            const Vec2& c = p[(i + 2) % n];
            const float ax = b.x - a.x, ay = b.y - a.y, bx = c.x - b.x, by = c.y - b.y;
            const float la = std::sqrt(ax * ax + ay * ay), lb = std::sqrt(bx * bx + by * by);
            if (la < 1e-6f || lb < 1e-6f || (ax * bx + ay * by) / (la * lb) < 0.97f) {
                disc(b, h);
            }
        }
        if (s.cap == Cap::ROUND && first) {
            disc(a, h);
        }
        if (s.cap == Cap::ROUND && last) {
            disc(b, h);
        }
    }
}
void SoftCanvas::stroke(const PathBatch& paths, const LineStyle& s)
{
    if (s.width <= 0.05f) {
        return;
    }
    float period = 0;
    for (int i = 0; i < s.dashCount && s.dash; i++) {
        period += s.dash[i];
    }
    const bool dashed = s.dash && s.dashCount >= 2 && period > 0.1f;
    uint32_t begin = 0;
    for (size_t k = 0; k < paths.ends.size(); k++) {
        const Vec2* p = paths.pts.data() + begin;
        const int n = (int)(paths.ends[k] - begin);
        begin = paths.ends[k];
        if (n < 2) {
            continue;
        }
        const bool closed = paths.closed[k] != 0;
        if (!dashed) {
            strokeRun(p, n, closed, s);
            continue;
        }
        /* dashes: walk the line, and stroke a piece for each "on" */
        int di = 0;
        float left = std::max(s.dash[0], 0.05f);
        bool on = true;
        _tmp.clear();
        _tmp.push_back(p[0]);
        const int segs = closed ? n : n - 1;
        for (int i = 0; i < segs; i++) {
            const Vec2 from = p[i], to = p[(i + 1) % n];
            const float total = std::hypot(to.x - from.x, to.y - from.y);
            float len = total;
            while (len > 1e-4f) {
                const float step = std::min(len, left);
                const float t = (total - len + step) / total;
                const Vec2 q = { from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t };
                if (on) {
                    _tmp.push_back(q);
                }
                len -= step;
                left -= step;
                if (left <= 1e-4f) {
                    if (on && _tmp.size() >= 2) {
                        strokeRun(_tmp.data(), (int)_tmp.size(), false, s);
                    }
                    _tmp.clear();
                    di = (di + 1) % s.dashCount;
                    left = std::max(s.dash[di], 0.05f);
                    on = !on;
                    if (on) {
                        _tmp.push_back(q);
                    }
                }
            }
        }
        if (on && _tmp.size() >= 2) {
            strokeRun(_tmp.data(), (int)_tmp.size(), false, s);
        }
    }
    resolve(s.color);
}
} // namespace MapTile
