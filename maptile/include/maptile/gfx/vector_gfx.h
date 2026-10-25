/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_GFX_VECTOR_GFX_H
#define MAPTILE_GFX_VECTOR_GFX_H

#include "maptile/types.h"

namespace MapTile {

enum class FillRule { NON_ZERO,
    EVEN_ODD };

class VectorGfx {
public:
    virtual ~VectorGfx() = default;

    /* Backend capability */
    virtual bool hasNativeStroke() const { return true; }
    void setForceStrokeToPath(bool force) { _forceStrokeToPath = force; }
    bool isStrokeToPathForced() const { return _forceStrokeToPath; }

    /* Lifecycle */
    virtual void begin() = 0;
    virtual void end() = 0;
    virtual void resetTransform(float scale) = 0;

    /* Path operations */
    virtual void pathClear() = 0;
    virtual void pathMoveTo(float x, float y) = 0;
    virtual void pathLineTo(float x, float y) = 0;
    virtual void pathClose() = 0;
    virtual void pathAddRect(float x1, float y1, float x2, float y2) = 0;
    virtual void pathAddCircle(float cx, float cy, float rx, float ry) = 0;

    /* Style */
    virtual void setFillColor(const Color& color) = 0;
    virtual void setFillOpacity(uint8_t opa) = 0;
    virtual void setFillRule(FillRule rule) = 0;
    virtual void setStrokeColor(const Color& color) = 0;
    virtual void setStrokeWidth(float width) = 0;
    virtual void setStrokeOpacity(uint8_t opa) = 0;

    /* Submit */
    virtual void fillPath() = 0;

private:
    bool _forceStrokeToPath = false;
};

} // namespace MapTile

#endif // MAPTILE_GFX_VECTOR_GFX_H
