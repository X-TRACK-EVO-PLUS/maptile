/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_LVGL_VECTOR_GFX_H
#define MAPTILE_LVGL_VECTOR_GFX_H

#include "lvgl/lvgl.h"
#include "maptile/gfx/vector_gfx.h"

namespace MapTile {

class LvglVectorGfx : public VectorGfx {
public:
    LvglVectorGfx(lv_layer_t* layer);
    ~LvglVectorGfx();

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
    lv_layer_t* _layer;
    lv_draw_vector_dsc_t* _vectorDsc;
    lv_vector_path_t* _vectorPath;
};

} // namespace MapTile

#endif // MAPTILE_LVGL_VECTOR_GFX_H
