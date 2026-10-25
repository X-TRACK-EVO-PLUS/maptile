/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_PLUTOVG_TEXT_GFX_H
#define MAPTILE_PLUTOVG_TEXT_GFX_H

#include "maptile/gfx/text_gfx.h"
#include <plutovg.h>

namespace MapTile {

class PlutoVGTextGfx : public TextGfx {
public:
    PlutoVGTextGfx(plutovg_canvas_t* canvas, plutovg_font_face_t* face, float fontSize);
    ~PlutoVGTextGfx();

    void drawLabel(float x, float y, const char* text,
        const Color& color, float maxWidth = 0) override;
    void drawIcon(float x, float y, const char* icon,
        const Color& color) override;
    TextMetrics measureText(const char* text, float maxWidth = 0) override;

private:
    plutovg_canvas_t* _canvas;
    plutovg_font_face_t* _face;
    float _fontSize;
};

} // namespace MapTile

#endif // MAPTILE_PLUTOVG_TEXT_GFX_H
