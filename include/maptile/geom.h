/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_GEOM_H
#define MAPTILE_GEOM_H
#include <cmath>
#include <cstdint>

namespace MapTile {

struct Vec2 {
    float x = 0;
    float y = 0;
};

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    static Color rgb(uint32_t hex)
    {
        return { uint8_t(hex >> 16), uint8_t(hex >> 8), uint8_t(hex), 255 };
    }
    /* "#rgb", "#rrggbb" or "#rrggbbaa" */
    static bool parse(const char* text, Color& out);
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

/* 2D affine transform: x' = a*x + c*y + e, y' = b*x + d*y + f */
struct Matrix {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    static Matrix translate(float tx, float ty) { return { 1, 0, 0, 1, tx, ty }; }
    static Matrix scale(float sx, float sy) { return { sx, 0, 0, sy, 0, 0 }; }
    /* clockwise on a y-down screen, degrees */
    static Matrix rotate(float degrees)
    {
        float r = degrees * 3.14159265358979f / 180.0f;
        float cs = std::cos(r), sn = std::sin(r);
        return { cs, sn, -sn, cs, 0, 0 };
    }
    Vec2 apply(Vec2 p) const { return { a * p.x + c * p.y + e, b * p.x + d * p.y + f }; }
    /* (*this * o).apply(p) == this->apply(o.apply(p)) */
    Matrix operator*(const Matrix& o) const
    {
        return { a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d,
            a * o.e + c * o.f + e, b * o.e + d * o.f + f };
    }
    Matrix inverse() const
    {
        float det = a * d - b * c;
        float id = det != 0 ? 1.0f / det : 0.0f;
        Matrix m { d * id, -b * id, -c * id, a * id, 0, 0 };
        m.e = -(m.a * e + m.c * f);
        m.f = -(m.b * e + m.d * f);
        return m;
    }
};

struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool intersects(const Rect& o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
};

} // namespace MapTile

#endif // MAPTILE_GEOM_H
