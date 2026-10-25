/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_LVGL_CANVAS_H
#define MAPTILE_LVGL_CANVAS_H
#include "lvgl/lvgl.h"
#include "maptile/canvas.h"
#include <functional>

namespace MapTile {

/*
 * Draws onto an LVGL 9.6 layer with lv_draw_vector (ThorVG on the software
 * renderer) and lv_draw_label. Create one inside a draw event handler (or
 * wherever you hold an lv_layer_t) and pass it to MapRenderer::render().
 *
 * `origin` is where the map viewport's top-left corner sits in layer
 * coordinates. The font for each text role is chosen by the application,
 * which is where font families, variable weights and theme changes live.
 */
class LvglCanvas : public Canvas {
public:
    using FontResolver = std::function<const lv_font_t*(const TextStyle&)>;

    LvglCanvas(lv_layer_t* layer, Vec2 origin, FontResolver fonts)
        : _layer(layer)
        , _origin(origin)
        , _fonts(std::move(fonts))
    {
    }

    void beginFrame(float width, float height, const Color& background) override;
    void fill(const PathBatch& rings, const Color& color) override;
    void stroke(const PathBatch& paths, const LineStyle& style) override;
    Vec2 measureText(const char* utf8, const TextStyle& style) override;
    void drawText(Vec2 center, const char* utf8, const TextStyle& style) override;
    void endFrame() override { }

private:
    lv_vector_path_t* buildPath(const PathBatch& b);
    void drawLabel(Vec2 center, const char* utf8, const lv_font_t* font, const Color& color, lv_point_t size);

    lv_layer_t* _layer;
    Vec2 _origin;
    FontResolver _fonts;
};

} // namespace MapTile

#endif // MAPTILE_LVGL_CANVAS_H
