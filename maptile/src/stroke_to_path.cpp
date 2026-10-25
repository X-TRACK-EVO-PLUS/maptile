/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile/stroke_to_path.h"
#include <cmath>

using namespace MapTile;

/* Vector math helpers */
static inline float vec_length(float dx, float dy)
{
    return sqrtf(dx * dx + dy * dy);
}

static inline void vec_normalize(float dx, float dy, float* nx, float* ny)
{
    float len = vec_length(dx, dy);
    if (len < 1e-6f) {
        *nx = 0;
        *ny = 0;
        return;
    }
    *nx = dx / len;
    *ny = dy / len;
}

/* Emit a closed rectangle for segment P0→P1 with half-width h */
static void emitSegmentRect(VectorGfx& gfx,
    float p0x, float p0y, float p1x, float p1y, float h)
{
    float dx = p1x - p0x;
    float dy = p1y - p0y;
    float nx, ny;
    vec_normalize(dx, dy, &nx, &ny);

    /* Normal perpendicular to segment direction (rotated 90°) */
    float px = -ny * h;
    float py = nx * h;

    /* Four corners of the rectangle */
    float ax = p0x + px, ay = p0y + py; /* A = P0 + normal * h */
    float bx = p0x - px, by = p0y - py; /* B = P0 - normal * h */
    float cx = p1x - px, cy = p1y - py; /* C = P1 - normal * h */
    float dxx = p1x + px, dxy = p1y + py; /* D = P1 + normal * h */

    gfx.pathMoveTo(ax, ay);
    gfx.pathLineTo(bx, by);
    gfx.pathLineTo(cx, cy);
    gfx.pathLineTo(dxx, dxy);
    gfx.pathClose();
}

/* Emit a bevel triangle at join point */
static void emitBevelJoin(VectorGfx& gfx,
    float p0x, float p0y, float p1x, float p1y,
    float p2x, float p2y, float joinX, float joinY, float h)
{
    /* Normals of segment 0→1 and 1→2 */
    float dx0 = p1x - p0x, dy0 = p1y - p0y;
    float dx1 = p2x - p1x, dy1 = p2y - p1y;
    float nx0, ny0, nx1, ny1;
    vec_normalize(dx0, dy0, &nx0, &ny0);
    vec_normalize(dx1, dy1, &nx1, &ny1);

    /* Determine which side the outer edge is on using cross product */
    float cross = nx0 * ny1 - ny0 * nx1;

    float outerX0, outerY0, outerX1, outerY1;
    if (cross > 0) {
        /* Left turn: outer is on the right (-normal) */
        outerX0 = joinX - (-ny0) * h;
        outerY0 = joinY - nx0 * h;
        outerX1 = joinX - (-ny1) * h;
        outerY1 = joinY - nx1 * h;
    } else {
        /* Right turn: outer is on the left (+normal) */
        outerX0 = joinX + (-ny0) * h;
        outerY0 = joinY + nx0 * h;
        outerX1 = joinX + (-ny1) * h;
        outerY1 = joinY + nx1 * h;
    }

    gfx.pathMoveTo(outerX0, outerY0);
    gfx.pathLineTo(joinX, joinY);
    gfx.pathLineTo(outerX1, outerY1);
    gfx.pathClose();
}

void StrokeToPath::expand(VectorGfx& gfx,
    const PointF* points, int count,
    const StrokeParams& params)
{
    if (count < 2)
        return;

    float h = params.width / 2.0f;

    /* 1. Emit rectangles for each segment */
    for (int i = 0; i < count - 1; i++) {
        emitSegmentRect(gfx,
            points[i].x, points[i].y,
            points[i + 1].x, points[i + 1].y, h);
    }

    /* 2. Emit joins at each interior vertex */
    for (int i = 1; i < count - 1; i++) {
        switch (params.join) {
        case StrokeJoin::ROUND:
            gfx.pathAddCircle(points[i].x, points[i].y, h, h);
            break;
        case StrokeJoin::BEVEL:
            emitBevelJoin(gfx,
                points[i - 1].x, points[i - 1].y,
                points[i].x, points[i].y,
                points[i + 1].x, points[i + 1].y,
                points[i].x, points[i].y, h);
            break;
        case StrokeJoin::MITER:
            /* TODO: implement miter with limit, fallback to bevel for now */
            emitBevelJoin(gfx,
                points[i - 1].x, points[i - 1].y,
                points[i].x, points[i].y,
                points[i + 1].x, points[i + 1].y,
                points[i].x, points[i].y, h);
            break;
        }
    }

    /* 3. Emit caps at start and end */
    switch (params.cap) {
    case StrokeCap::ROUND:
        gfx.pathAddCircle(points[0].x, points[0].y, h, h);
        gfx.pathAddCircle(points[count - 1].x, points[count - 1].y, h, h);
        break;
    case StrokeCap::SQUARE: {
        /* Extend start */
        float dx0 = points[1].x - points[0].x;
        float dy0 = points[1].y - points[0].y;
        float nx0, ny0;
        vec_normalize(dx0, dy0, &nx0, &ny0);
        float sx = points[0].x - nx0 * h;
        float sy = points[0].y - ny0 * h;
        emitSegmentRect(gfx, sx, sy, points[0].x, points[0].y, h);

        /* Extend end */
        int last = count - 1;
        float dxL = points[last].x - points[last - 1].x;
        float dyL = points[last].y - points[last - 1].y;
        float nxL, nyL;
        vec_normalize(dxL, dyL, &nxL, &nyL);
        float ex = points[last].x + nxL * h;
        float ey = points[last].y + nyL * h;
        emitSegmentRect(gfx, points[last].x, points[last].y, ex, ey, h);
    } break;
    case StrokeCap::BUTT:
        /* No extra geometry */
        break;
    }
}
