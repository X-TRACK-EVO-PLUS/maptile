/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_TYPES_H
#define MAPTILE_TYPES_H

#include "vector_tile.pb.h"
#include <cstdint>
#include <map>
#include <string>

namespace MapTile {

struct Color {
    uint8_t r, g, b, a;

    Color()
        : r(0)
        , g(0)
        , b(0)
        , a(255)
    {
    }
    Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : r(r)
        , g(g)
        , b(b)
        , a(a)
    {
    }

    static Color fromHex(uint32_t hex)
    {
        return Color(
            (hex >> 16) & 0xFF,
            (hex >> 8) & 0xFF,
            hex & 0xFF);
    }

    static Color mix(const Color& c1, const Color& c2, uint8_t ratio)
    {
        return Color(
            (c1.r * ratio + c2.r * (255 - ratio)) / 255,
            (c1.g * ratio + c2.g * (255 - ratio)) / 255,
            (c1.b * ratio + c2.b * (255 - ratio)) / 255,
            (c1.a * ratio + c2.a * (255 - ratio)) / 255);
    }
};

struct PointF {
    float x, y;
};

struct RectF {
    float x1, y1, x2, y2;
};

struct FillStyle {
    Color color;
    uint8_t opacity;
    FillStyle()
        : opacity(255)
    {
    }
};

struct StrokeStyle {
    Color color;
    float width;
    uint8_t opacity;
    StrokeStyle()
        : width(1.0f)
        , opacity(255)
    {
    }
};

struct LabelStyle {
    Color color;
    float fontSize;
    const char* icon; /* UTF-8 icon string, nullptr if no icon */
    LabelStyle()
        : fontSize(12.0f)
        , icon(nullptr)
    {
    }
};

typedef std::map<std::string, vector_tile::Tile_Value> FeatureProp;

} // namespace MapTile

#endif // MAPTILE_TYPES_H
