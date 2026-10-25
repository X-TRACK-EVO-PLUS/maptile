/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_TILE_PARSER_H
#define MAPTILE_TILE_PARSER_H

#include "maptile/style.h"
#include "maptile/types.h"
#include "vector_tile.pb.h"
#include <vector>

namespace MapTile {

struct PathCommand {
    enum Type { MOVE_TO,
        LINE_TO,
        CLOSE };
    Type type;
    float x, y;
};

struct ParsedFeature {
    enum GeomType { UNKNOWN,
        POINT,
        LINE,
        POLYGON };
    GeomType geomType;
    std::string layerName;
    FeatureProp properties;
    std::vector<PathCommand> commands;
};

struct ParsedTile {
    std::vector<ParsedFeature> features;
    std::vector<ParsedFeature> labels;
    uint32_t extent;
};

class TileParser {
public:
    static ParsedTile parse(const vector_tile::Tile& tile,
        StyleBase& style, float zoom,
        const char** layerSequence = nullptr,
        const char** labelSequence = nullptr);
};

} // namespace MapTile

#endif // MAPTILE_TILE_PARSER_H
