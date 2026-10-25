/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_LVGL_TEXT_GFX_H
#define MAPTILE_LVGL_TEXT_GFX_H

#include "lvgl/lvgl.h"
#include "maptile/gfx/text_gfx.h"

namespace MapTile {

class LvglTextGfx : public TextGfx {
public:
    LvglTextGfx(lv_layer_t* layer, const lv_font_t* font);
    ~LvglTextGfx();

    void drawLabel(float x, float y, const char* text,
        const Color& color, float maxWidth = 0) override;
    void drawIcon(float x, float y, const char* icon,
        const Color& color) override;
    TextMetrics measureText(const char* text, float maxWidth = 0) override;

private:
    lv_layer_t* _layer;
    const lv_font_t* _font;
};

} // namespace MapTile

#endif // MAPTILE_LVGL_TEXT_GFX_H
