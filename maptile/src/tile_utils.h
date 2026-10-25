/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef MAPTILE_TILE_UTILS_H
#define MAPTILE_TILE_UTILS_H

#include "vector_tile.pb.h"
#include <map>
namespace MapTile {

class Utils {
public:
public:
    enum class GEOMETRY_CMD {
        NONE = 0,
        MOVE_TO = 1,
        LINE_TO = 2,
        CLOSE_PATH = 7,
    };

public:
    static void buildTileLayerFeatureProperties(
        const vector_tile::Tile_Layer& layer,
        const vector_tile::Tile_Feature& feature,
        std::map<std::string, vector_tile::Tile_Value>& mapOutput);

    static void parseGeometryCmd(uint32_t geometryCmd, GEOMETRY_CMD* cmd, uint32_t* count);
    static void parseNextPoint(const vector_tile::Tile_Feature& tileFearture, int index, int* x, int* y);
    static int zigzag(int value);
};

}; // namespace MapTile

#endif // MAPTILE_TILE_UTILS_H
