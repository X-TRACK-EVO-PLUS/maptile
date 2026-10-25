/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile_plutovg/plutovg_gfx.h"

using namespace MapTile;

PlutoVGGfx::PlutoVGGfx(plutovg_canvas_t* canvas)
    : _canvas(canvas)
    , _fillOpacity(255)
    , _strokeWidth(1.0f)
    , _strokeOpacity(255)
{
}

PlutoVGGfx::~PlutoVGGfx()
{
}

void PlutoVGGfx::begin()
{
    plutovg_canvas_save(_canvas);
}

void PlutoVGGfx::end()
{
    plutovg_canvas_restore(_canvas);
}

void PlutoVGGfx::resetTransform(float scale)
{
    plutovg_canvas_reset_matrix(_canvas);
    plutovg_canvas_scale(_canvas, scale, scale);
}

void PlutoVGGfx::pathClear()
{
    plutovg_canvas_new_path(_canvas);
}

void PlutoVGGfx::pathMoveTo(float x, float y)
{
    plutovg_canvas_move_to(_canvas, x, y);
}

void PlutoVGGfx::pathLineTo(float x, float y)
{
    plutovg_canvas_line_to(_canvas, x, y);
}

void PlutoVGGfx::pathClose()
{
    plutovg_canvas_close_path(_canvas);
}

void PlutoVGGfx::pathAddRect(float x1, float y1, float x2, float y2)
{
    plutovg_canvas_rect(_canvas, x1, y1, x2 - x1, y2 - y1);
}

void PlutoVGGfx::pathAddCircle(float cx, float cy, float rx, float ry)
{
    plutovg_canvas_ellipse(_canvas, cx, cy, rx, ry);
}

void PlutoVGGfx::setFillColor(const Color& color)
{
    _fillColor = color;
}

void PlutoVGGfx::setFillOpacity(uint8_t opa)
{
    _fillOpacity = opa;
}

void PlutoVGGfx::setFillRule(FillRule rule)
{
    plutovg_canvas_set_fill_rule(_canvas,
        rule == FillRule::EVEN_ODD ? PLUTOVG_FILL_RULE_EVEN_ODD : PLUTOVG_FILL_RULE_NON_ZERO);
}

void PlutoVGGfx::setStrokeColor(const Color& color)
{
    _strokeColor = color;
}

void PlutoVGGfx::setStrokeWidth(float width)
{
    _strokeWidth = width;
}

void PlutoVGGfx::setStrokeOpacity(uint8_t opa)
{
    _strokeOpacity = opa;
}

void PlutoVGGfx::fillPath()
{
    /* Fill */
    if (_fillOpacity > 0) {
        plutovg_canvas_set_rgba(_canvas,
            _fillColor.r / 255.0f, _fillColor.g / 255.0f,
            _fillColor.b / 255.0f, _fillOpacity / 255.0f);
        plutovg_canvas_fill(_canvas);
    }

    /* Stroke */
    if (_strokeOpacity > 0 && _strokeWidth > 0) {
        plutovg_canvas_set_rgba(_canvas,
            _strokeColor.r / 255.0f, _strokeColor.g / 255.0f,
            _strokeColor.b / 255.0f, _strokeOpacity / 255.0f);
        plutovg_canvas_set_line_width(_canvas, _strokeWidth);
        plutovg_canvas_stroke(_canvas);
    }
}
