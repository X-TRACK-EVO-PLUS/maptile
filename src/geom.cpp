/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile/geom.h"
#include <cstdlib>
#include <cstring>

namespace MapTile {

static int hexVal(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool Color::parse(const char* text, Color& out)
{
    if (!text || text[0] != '#')
        return false;
    size_t n = std::strlen(text + 1);
    if (n != 3 && n != 6 && n != 8)
        return false;
    int v[8];
    for (size_t i = 0; i < n; i++) {
        v[i] = hexVal(text[1 + i]);
        if (v[i] < 0)
            return false;
    }
    if (n == 3) {
        out = { uint8_t(v[0] * 17), uint8_t(v[1] * 17), uint8_t(v[2] * 17), 255 };
    } else {
        out = { uint8_t(v[0] * 16 + v[1]), uint8_t(v[2] * 16 + v[3]), uint8_t(v[4] * 16 + v[5]),
            n == 8 ? uint8_t(v[6] * 16 + v[7]) : uint8_t(255) };
    }
    return true;
}

} // namespace MapTile
