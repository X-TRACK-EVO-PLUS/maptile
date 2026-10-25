/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "lvgl_canvas.h"

namespace MapTile {

namespace {
    lv_color_t lvColor(const Color& c) { return lv_color_make(c.r, c.g, c.b); }
}

void LvglCanvas::beginFrame(float width, float height, const Color& background)
{
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = lvColor(background);
    rect.bg_opa = LV_OPA_COVER;
    lv_area_t area;
    area.x1 = (int32_t)_origin.x;
    area.y1 = (int32_t)_origin.y;
    area.x2 = (int32_t)(_origin.x + width) - 1;
    area.y2 = (int32_t)(_origin.y + height) - 1;
    lv_draw_rect(_layer, &rect, &area);
}

lv_vector_path_t* LvglCanvas::buildPath(const PathBatch& b)
{
    lv_vector_path_t* path = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_MEDIUM);
    uint32_t i = 0;
    for (size_t s = 0; s < b.ends.size(); s++) {
        for (bool first = true; i < b.ends[s]; i++, first = false) {
            lv_fpoint_t p = { b.pts[i].x + _origin.x, b.pts[i].y + _origin.y };
            if (first)
                lv_vector_path_move_to(path, &p);
            else
                lv_vector_path_line_to(path, &p);
        }
        if (b.closed[s])
            lv_vector_path_close(path);
    }
    return path;
}

void LvglCanvas::fill(const PathBatch& rings, const Color& color)
{
    lv_vector_path_t* path = buildPath(rings);
    lv_draw_vector_dsc_t* dsc = lv_draw_vector_dsc_create(_layer);
    lv_draw_vector_dsc_set_fill_color(dsc, lvColor(color));
    lv_draw_vector_dsc_set_fill_opa(dsc, color.a);
    lv_draw_vector_dsc_set_fill_rule(dsc, LV_VECTOR_FILL_NONZERO);
    lv_draw_vector_dsc_set_stroke_opa(dsc, LV_OPA_TRANSP);
    lv_draw_vector_dsc_add_path(dsc, path);
    lv_draw_vector(dsc);
    lv_draw_vector_dsc_delete(dsc);
    lv_vector_path_delete(path);
}

void LvglCanvas::stroke(const PathBatch& paths, const LineStyle& s)
{
    lv_vector_path_t* path = buildPath(paths);
    lv_draw_vector_dsc_t* dsc = lv_draw_vector_dsc_create(_layer);
    lv_draw_vector_dsc_set_fill_opa(dsc, LV_OPA_TRANSP);
    lv_draw_vector_dsc_set_stroke_color(dsc, lvColor(s.color));
    lv_draw_vector_dsc_set_stroke_opa(dsc, s.color.a);
    lv_draw_vector_dsc_set_stroke_width(dsc, s.width);
    lv_draw_vector_dsc_set_stroke_cap(dsc, s.cap == Cap::BUTT ? LV_VECTOR_STROKE_CAP_BUTT : s.cap == Cap::SQUARE ? LV_VECTOR_STROKE_CAP_SQUARE
                                                                                                                 : LV_VECTOR_STROKE_CAP_ROUND);
    lv_draw_vector_dsc_set_stroke_join(dsc, s.join == Join::MITER ? LV_VECTOR_STROKE_JOIN_MITER : s.join == Join::BEVEL ? LV_VECTOR_STROKE_JOIN_BEVEL
                                                                                                                        : LV_VECTOR_STROKE_JOIN_ROUND);
    if (s.dash && s.dashCount > 0)
        lv_draw_vector_dsc_set_stroke_dash(dsc, const_cast<float*>(s.dash), (uint16_t)s.dashCount);
    lv_draw_vector_dsc_add_path(dsc, path);
    lv_draw_vector(dsc);
    lv_draw_vector_dsc_delete(dsc);
    lv_vector_path_delete(path);
}

Vec2 LvglCanvas::measureText(const char* utf8, const TextStyle& style)
{
    const lv_font_t* font = _fonts(style);
    lv_point_t size;
    lv_text_get_size(&size, utf8, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return { (float)size.x, (float)size.y };
}

void LvglCanvas::drawLabel(Vec2 c, const char* utf8, const lv_font_t* font, const Color& color, lv_point_t size)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = lvColor(color);
    dsc.opa = color.a;
    dsc.font = font;
    dsc.text = utf8;
    dsc.text_local = 1; /* the draw task may outlive the caller's string */
    lv_area_t area;
    area.x1 = (int32_t)(c.x + _origin.x - size.x / 2.0f);
    area.y1 = (int32_t)(c.y + _origin.y - size.y / 2.0f);
    area.x2 = area.x1 + size.x - 1;
    area.y2 = area.y1 + size.y - 1;
    lv_draw_label(_layer, &dsc, &area);
}

void LvglCanvas::drawText(Vec2 c, const char* utf8, const TextStyle& style)
{
    const lv_font_t* font = _fonts(style);
    lv_point_t size;
    lv_text_get_size(&size, utf8, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (style.halo.a) {
        static const float off[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        for (const auto& o : off)
            drawLabel({ c.x + o[0], c.y + o[1] }, utf8, font, style.halo, size);
    }
    drawLabel(c, utf8, font, style.color, size);
}

} // namespace MapTile
