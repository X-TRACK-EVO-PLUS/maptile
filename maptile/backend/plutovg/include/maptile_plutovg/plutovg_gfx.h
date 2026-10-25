/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_PLUTOVG_GFX_H
#define MAPTILE_PLUTOVG_GFX_H

#include "maptile/gfx/vector_gfx.h"
#include <plutovg.h>

namespace MapTile {

class PlutoVGGfx : public VectorGfx {
public:
    PlutoVGGfx(plutovg_canvas_t* canvas);
    ~PlutoVGGfx();

    void begin() override;
    void end() override;
    void resetTransform(float scale) override;

    void pathClear() override;
    void pathMoveTo(float x, float y) override;
    void pathLineTo(float x, float y) override;
    void pathClose() override;
    void pathAddRect(float x1, float y1, float x2, float y2) override;
    void pathAddCircle(float cx, float cy, float rx, float ry) override;

    void setFillColor(const Color& color) override;
    void setFillOpacity(uint8_t opa) override;
    void setFillRule(FillRule rule) override;
    void setStrokeColor(const Color& color) override;
    void setStrokeWidth(float width) override;
    void setStrokeOpacity(uint8_t opa) override;

    void fillPath() override;

private:
    plutovg_canvas_t* _canvas;
    Color _fillColor;
    uint8_t _fillOpacity;
    Color _strokeColor;
    float _strokeWidth;
    uint8_t _strokeOpacity;
};

} // namespace MapTile

#endif // MAPTILE_PLUTOVG_GFX_H
