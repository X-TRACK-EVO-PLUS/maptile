/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile_plutovg/plutovg_text_gfx.h"
#include <cstring>

using namespace MapTile;

PlutoVGTextGfx::PlutoVGTextGfx(plutovg_canvas_t* canvas, plutovg_font_face_t* face, float fontSize)
    : _canvas(canvas)
    , _face(face)
    , _fontSize(fontSize)
{
}

PlutoVGTextGfx::~PlutoVGTextGfx()
{
}

void PlutoVGTextGfx::drawLabel(float x, float y, const char* text,
    const Color& color, float maxWidth)
{
    plutovg_canvas_save(_canvas);
    plutovg_canvas_set_font(_canvas, _face, _fontSize);
    plutovg_canvas_set_rgba(_canvas,
        color.r / 255.0f, color.g / 255.0f,
        color.b / 255.0f, color.a / 255.0f);

    /* Measure to center horizontally */
    plutovg_rect_t extents;
    int len = (int)strlen(text);
    plutovg_font_face_text_extents(_face, _fontSize, text, len,
        PLUTOVG_TEXT_ENCODING_UTF8, &extents);

    float drawX = x - extents.w / 2;
    float drawY = y + extents.h;
    plutovg_canvas_fill_text(_canvas, text, len,
        PLUTOVG_TEXT_ENCODING_UTF8, drawX, drawY);
    plutovg_canvas_restore(_canvas);
}

void PlutoVGTextGfx::drawIcon(float x, float y, const char* icon,
    const Color& color)
{
    drawLabel(x, y, icon, color);
}

TextMetrics PlutoVGTextGfx::measureText(const char* text, float maxWidth)
{
    plutovg_rect_t extents;
    int len = (int)strlen(text);
    plutovg_font_face_text_extents(_face, _fontSize, text, len,
        PLUTOVG_TEXT_ENCODING_UTF8, &extents);
    TextMetrics metrics;
    metrics.width = extents.w;
    metrics.height = extents.h;
    return metrics;
}
