/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_STROKE_TO_PATH_H
#define MAPTILE_STROKE_TO_PATH_H

#include "maptile/gfx/vector_gfx.h"
#include "maptile/types.h"

namespace MapTile {

enum class StrokeCap { BUTT,
    ROUND,
    SQUARE };
enum class StrokeJoin { MITER,
    BEVEL,
    ROUND };

struct StrokeParams {
    float width;
    StrokeCap cap;
    StrokeJoin join;
    float miterLimit;

    StrokeParams()
        : width(1.0f)
        , cap(StrokeCap::ROUND)
        , join(StrokeJoin::ROUND)
        , miterLimit(4.0f)
    {
    }
};

class StrokeToPath {
public:
    static void expand(VectorGfx& gfx,
        const PointF* points, int count,
        const StrokeParams& params);
};

} // namespace MapTile

#endif // MAPTILE_STROKE_TO_PATH_H
