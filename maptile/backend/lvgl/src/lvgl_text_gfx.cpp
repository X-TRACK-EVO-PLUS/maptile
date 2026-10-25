/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile_lvgl/lvgl_text_gfx.h"

using namespace MapTile;

LvglTextGfx::LvglTextGfx(lv_layer_t* layer, const lv_font_t* font)
    : _layer(layer)
    , _font(font)
{
}

LvglTextGfx::~LvglTextGfx()
{
}

void LvglTextGfx::drawLabel(float x, float y, const char* text,
    const Color& color, float maxWidth)
{
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_make(color.r, color.g, color.b);
    label_dsc.text = lv_strdup(text);
    label_dsc.font = _font;

    lv_point_t size;
    int32_t mw = maxWidth > 0 ? (int32_t)maxWidth : 150;
    lv_text_get_size(&size, label_dsc.text, label_dsc.font,
        label_dsc.letter_space, label_dsc.line_space, mw, LV_TEXT_FLAG_FIT);

    lv_area_t coords;
    coords.x1 = (int32_t)(x - size.x / 2);
    coords.y1 = (int32_t)y;
    coords.x2 = coords.x1 + size.x;
    coords.y2 = coords.y1 + size.y;
    lv_draw_label(_layer, &label_dsc, &coords);
}

void LvglTextGfx::drawIcon(float x, float y, const char* icon,
    const Color& color)
{
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_make(color.r, color.g, color.b);
    label_dsc.text = icon;
    label_dsc.font = _font;

    lv_point_t size;
    lv_text_get_size(&size, label_dsc.text, label_dsc.font,
        label_dsc.letter_space, label_dsc.line_space, 150, LV_TEXT_FLAG_FIT);

    lv_area_t coords;
    coords.x1 = (int32_t)(x - size.x / 2);
    coords.y1 = (int32_t)(y - size.y / 2);
    coords.x2 = coords.x1 + size.x;
    coords.y2 = coords.y1 + size.y;
    lv_draw_label(_layer, &label_dsc, &coords);
}

TextMetrics LvglTextGfx::measureText(const char* text, float maxWidth)
{
    lv_point_t size;
    int32_t mw = maxWidth > 0 ? (int32_t)maxWidth : 150;
    lv_text_get_size(&size, text, _font, 0, 0, mw, LV_TEXT_FLAG_FIT);
    TextMetrics metrics;
    metrics.width = (float)size.x;
    metrics.height = (float)size.y;
    return metrics;
}
