/*
 * MIT License
 * Copyright (c) 2024 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_TILE_RENDERER_H
#define MAPTILE_TILE_RENDERER_H

#include "maptile/gfx/text_gfx.h"
#include "maptile/gfx/vector_gfx.h"
#include "maptile/stroke_to_path.h"
#include "maptile/style.h"
#include "maptile/tile_parser.h"
#include <vector>

namespace MapTile {

class TileRenderer {
public:
    TileRenderer(VectorGfx& gfx, TextGfx& textGfx, StyleBase& style, float zoom);
    ~TileRenderer();

    void setTargetSize(float size) { _targetSize = size; }
    float getTargetSize() const { return _targetSize; }
    void render(const ParsedTile& tile);

private:
    float getScale() const { return _targetSize / (float)_extent; }

    void renderFeature(const ParsedFeature& feature);
    void renderLabel(const ParsedFeature& label);
    void drawPolygon(const ParsedFeature& feature);
    void drawLine(const ParsedFeature& feature);
    void drawPoint(const ParsedFeature& feature);

    VectorGfx& _gfx;
    TextGfx& _textGfx;
    StyleBase& _style;
    float _zoom;
    float _targetSize;
    uint32_t _extent;
    std::vector<RectF> _labelBoxes;
};

} // namespace MapTile

#endif // MAPTILE_TILE_RENDERER_H
