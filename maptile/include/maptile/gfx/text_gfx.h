/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_GFX_TEXT_GFX_H
#define MAPTILE_GFX_TEXT_GFX_H

#include "maptile/types.h"

namespace MapTile {

struct TextMetrics {
    float width;
    float height;
};

class TextGfx {
public:
    virtual ~TextGfx() = default;

    virtual void drawLabel(float x, float y, const char* text,
        const Color& color, float maxWidth = 0)
        = 0;
    virtual void drawIcon(float x, float y, const char* icon,
        const Color& color)
        = 0;
    virtual TextMetrics measureText(const char* text, float maxWidth = 0) = 0;
};

} // namespace MapTile

#endif // MAPTILE_GFX_TEXT_GFX_H
