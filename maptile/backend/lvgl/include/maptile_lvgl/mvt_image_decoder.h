/*
 * MIT License
 * Copyright (c) 2024 - 2026 _VIFEXTech
 */
#ifndef MAPTILE_MVT_IMAGE_DECODER_H
#define MAPTILE_MVT_IMAGE_DECODER_H

#include "lvgl/lvgl.h"
#include <plutovg.h>

namespace MapTile {

class StyleBase;

class MvtImageDecoder {
public:
    /**
     * Register .mvt file decoder with LVGL.
     * @param style    Rendering style
     * @param font     LVGL text font (unused when PlutoVG font is set, can be NULL)
     * @param tileSize Tile render size in pixels (default 256)
     */
    static void install(StyleBase& style, lv_font_t* font, int tileSize = 256);

    /** Change rendering style and invalidate all cached tiles */
    static void setStyle(StyleBase& style);

    /** Change LVGL font and invalidate all cached tiles */
    static void setFont(lv_font_t* font);

    /** Set PlutoVG font face for text rendering */
    static void setPlutoVGFont(plutovg_font_face_t* face, float fontSize);

    /** Unregister the decoder */
    static void uninstall();
};

} // namespace MapTile

#endif // MAPTILE_MVT_IMAGE_DECODER_H
