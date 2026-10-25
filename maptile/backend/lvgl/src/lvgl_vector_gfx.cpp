/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile_lvgl/lvgl_vector_gfx.h"

using namespace MapTile;

LvglVectorGfx::LvglVectorGfx(lv_layer_t* layer)
    : _layer(layer)
    , _vectorDsc(nullptr)
    , _vectorPath(nullptr)
{
}

LvglVectorGfx::~LvglVectorGfx()
{
}

void LvglVectorGfx::begin()
{
    LV_ASSERT_NULL(_layer);
    _vectorDsc = lv_draw_vector_dsc_create(_layer);
    _vectorPath = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_HIGH);
    lv_draw_vector_dsc_identity(_vectorDsc);
}

void LvglVectorGfx::end()
{
    lv_draw_vector(_vectorDsc);
    lv_vector_path_delete(_vectorPath);
    _vectorPath = nullptr;
    lv_draw_vector_dsc_delete(_vectorDsc);
    _vectorDsc = nullptr;
}

void LvglVectorGfx::resetTransform(float scale)
{
    lv_matrix_t matrix;
    lv_matrix_identity(&matrix);
    lv_matrix_scale(&matrix, scale, scale);
    lv_draw_vector_dsc_set_transform(_vectorDsc, &matrix);
}

void LvglVectorGfx::pathClear()
{
    lv_vector_path_clear(_vectorPath);
}

void LvglVectorGfx::pathMoveTo(float x, float y)
{
    lv_fpoint_t fpoint = { x, y };
    lv_vector_path_move_to(_vectorPath, &fpoint);
}

void LvglVectorGfx::pathLineTo(float x, float y)
{
    lv_fpoint_t fpoint = { x, y };
    lv_vector_path_line_to(_vectorPath, &fpoint);
}

void LvglVectorGfx::pathClose()
{
    lv_vector_path_close(_vectorPath);
}

void LvglVectorGfx::pathAddRect(float x1, float y1, float x2, float y2)
{
    lv_area_t area;
    area.x1 = (int32_t)x1;
    area.y1 = (int32_t)y1;
    area.x2 = (int32_t)x2;
    area.y2 = (int32_t)y2;
    lv_vector_path_append_rect(_vectorPath, &area, 0, 0);
}

void LvglVectorGfx::pathAddCircle(float cx, float cy, float rx, float ry)
{
    lv_fpoint_t fpoint = { cx, cy };
    lv_vector_path_append_circle(_vectorPath, &fpoint, rx, ry);
}

void LvglVectorGfx::setFillColor(const Color& color)
{
    lv_draw_vector_dsc_set_fill_color(_vectorDsc, lv_color_make(color.r, color.g, color.b));
}

void LvglVectorGfx::setFillOpacity(uint8_t opa)
{
    lv_draw_vector_dsc_set_fill_opa(_vectorDsc, (lv_opa_t)opa);
}

void LvglVectorGfx::setFillRule(FillRule rule)
{
    lv_draw_vector_dsc_set_fill_rule(_vectorDsc,
        rule == FillRule::EVEN_ODD ? LV_VECTOR_FILL_EVENODD : LV_VECTOR_FILL_NONZERO);
}

void LvglVectorGfx::setStrokeColor(const Color& color)
{
    lv_draw_vector_dsc_set_stroke_color(_vectorDsc, lv_color_make(color.r, color.g, color.b));
}

void LvglVectorGfx::setStrokeWidth(float width)
{
    lv_draw_vector_dsc_set_stroke_width(_vectorDsc, width);
}

void LvglVectorGfx::setStrokeOpacity(uint8_t opa)
{
    lv_draw_vector_dsc_set_stroke_opa(_vectorDsc, (lv_opa_t)opa);
}

void LvglVectorGfx::fillPath()
{
    lv_draw_vector_dsc_add_path(_vectorDsc, _vectorPath);
}
