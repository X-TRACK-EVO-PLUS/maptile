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
#include "tile_utils.h"

using namespace vector_tile;
using namespace MapTile;

void Utils::buildTileLayerFeatureProperties(
    const Tile_Layer& layer,
    const Tile_Feature& feature,
    std::map<std::string, Tile_Value>& mapOutput)
{
    int tagCount = feature.tags_size();
    int keyCount = layer.keys_size();
    int valCount = layer.values_size();
    for (int i = 0; i + 1 < tagCount; i += 2) {
        int ki = feature.tags(i);
        int vi = feature.tags(i + 1);
        if (ki < keyCount && vi < valCount) {
            mapOutput[layer.keys(ki)] = layer.values(vi);
        }
    }
}

void Utils::parseGeometryCmd(uint32_t geometryCmd, GEOMETRY_CMD* cmd, uint32_t* count)
{
    *cmd = (GEOMETRY_CMD)(geometryCmd & 0x7);
    *count = geometryCmd >> 3;
}

void Utils::parseNextPoint(const vector_tile::Tile_Feature& tileFearture, int index, int* x, int* y)
{
    if (index + 1 >= tileFearture.geometry_size())
        return;
    *x = *x + Utils::zigzag(tileFearture.geometry(index));
    *y = *y + Utils::zigzag(tileFearture.geometry(index + 1));
}

int Utils::zigzag(int value)
{
    return (value >> 1) ^ (-(value & 1));
}
