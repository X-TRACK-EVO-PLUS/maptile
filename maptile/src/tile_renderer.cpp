/*
 * MIT License
 * Copyright (c) 2024 HanfG, _VIFEXTech
 */
#include "maptile/tile_renderer.h"
#include <cstring>
#include <vector>

using namespace MapTile;

TileRenderer::TileRenderer(VectorGfx& gfx, TextGfx& textGfx, StyleBase& style, float zoom)
    : _gfx(gfx)
    , _textGfx(textGfx)
    , _style(style)
    , _zoom(zoom)
    , _targetSize(512)
    , _extent(4096)
{
}

TileRenderer::~TileRenderer() { }

void TileRenderer::render(const ParsedTile& tile)
{
    _extent = tile.extent;
    _gfx.begin();

    /* Background */
    {
        StyleResult sr;
        FeatureProp empty;
        if (_style.getStyle(StyleType::BACKGROUND, "", empty, _zoom, sr)) {
            _gfx.pathClear();
            _gfx.pathAddRect(0, 0, _targetSize - 1, _targetSize - 1);
            _gfx.setFillColor(sr.fill.color);
            _gfx.setFillOpacity(sr.fill.opacity);
            _gfx.setStrokeOpacity(0);
            _gfx.setStrokeWidth(0);
            _gfx.fillPath();
        }
    }

    /* Features */
    _gfx.resetTransform(getScale());
    for (const auto& feature : tile.features) {
        renderFeature(feature);
    }

    _gfx.end();

    /* Labels with collision detection */
    _labelBoxes.clear();
    for (const auto& label : tile.labels) {
        renderLabel(label);
    }
}

void TileRenderer::renderFeature(const ParsedFeature& feature)
{
    switch (feature.geomType) {
    case ParsedFeature::POLYGON:
        drawPolygon(feature);
        break;
    case ParsedFeature::LINE:
        drawLine(feature);
        break;
    case ParsedFeature::POINT:
        drawPoint(feature);
        break;
    default:
        break;
    }
}

void TileRenderer::renderLabel(const ParsedFeature& label)
{
    auto nameIter = label.properties.find("name");
    if (nameIter == label.properties.end())
        return;

    const char* text = nameIter->second.string_value().c_str();
    float scale = _targetSize / (float)_extent;

    StyleResult sr;
    _style.getStyle(StyleType::LABEL, label.layerName, label.properties, _zoom, sr);

    for (const auto& cmd : label.commands) {
        if (cmd.type != PathCommand::MOVE_TO)
            continue;

        float x = cmd.x * scale;
        float y = cmd.y * scale;

        /* Measure total label bounds */
        TextMetrics textSize = _textGfx.measureText(text, 60);
        float totalW = textSize.width;
        float totalH = textSize.height;

        if (sr.label.icon) {
            TextMetrics iconSize = _textGfx.measureText(sr.label.icon);
            totalW = totalW > iconSize.width ? totalW : iconSize.width;
            totalH += iconSize.height + 2;
        }

        /* Collision check */
        RectF bbox;
        bbox.x1 = x - totalW / 2;
        bbox.y1 = y - totalH / 2;
        bbox.x2 = x + totalW / 2;
        bbox.y2 = y + totalH / 2;

        bool collides = false;
        for (const auto& occupied : _labelBoxes) {
            if (bbox.x1 < occupied.x2 && bbox.x2 > occupied.x1
                && bbox.y1 < occupied.y2 && bbox.y2 > occupied.y1) {
                collides = true;
                break;
            }
        }

        if (collides)
            break;

        _labelBoxes.push_back(bbox);

        /* Draw */
        if (sr.label.icon) {
            _textGfx.drawIcon(x, y, sr.label.icon, sr.label.color);
            TextMetrics iconSize = _textGfx.measureText(sr.label.icon);
            _textGfx.drawLabel(x, y + iconSize.height / 2 + 2, text, sr.label.color, 60);
        } else {
            _textGfx.drawLabel(x, y, text, sr.label.color);
        }
        break;
    }
}

void TileRenderer::drawPolygon(const ParsedFeature& feature)
{
    StyleResult sr;
    _style.getStyle(StyleType::FILL, feature.layerName, feature.properties, _zoom, sr);

    _gfx.pathClear();
    _gfx.setFillColor(sr.fill.color);
    _gfx.setFillOpacity(sr.fill.opacity);
    _gfx.setStrokeWidth(0);
    _gfx.setStrokeOpacity(0);

    for (const auto& cmd : feature.commands) {
        switch (cmd.type) {
        case PathCommand::MOVE_TO:
            _gfx.pathMoveTo(cmd.x, cmd.y);
            break;
        case PathCommand::LINE_TO:
            _gfx.pathLineTo(cmd.x, cmd.y);
            break;
        case PathCommand::CLOSE:
            _gfx.pathClose();
            break;
        }
    }
    _gfx.fillPath();
}

void TileRenderer::drawLine(const ParsedFeature& feature)
{
    StyleResult sr;
    _style.getStyle(StyleType::STROKE, feature.layerName, feature.properties, _zoom, sr);

    if (_gfx.hasNativeStroke() && !_gfx.isStrokeToPathForced()) {
        _gfx.pathClear();
        _gfx.setFillOpacity(0);
        _gfx.setStrokeColor(sr.stroke.color);
        _gfx.setStrokeWidth(sr.stroke.width);
        _gfx.setStrokeOpacity(sr.stroke.opacity);

        for (const auto& cmd : feature.commands) {
            switch (cmd.type) {
            case PathCommand::MOVE_TO:
                _gfx.pathMoveTo(cmd.x, cmd.y);
                break;
            case PathCommand::LINE_TO:
                _gfx.pathLineTo(cmd.x, cmd.y);
                break;
            case PathCommand::CLOSE:
                _gfx.pathClose();
                break;
            }
        }
        _gfx.fillPath();
    } else {
        StrokeParams params;
        params.width = sr.stroke.width;

        _gfx.pathClear();
        _gfx.setFillColor(sr.stroke.color);
        _gfx.setFillOpacity(sr.stroke.opacity);
        _gfx.setFillRule(FillRule::NON_ZERO);
        _gfx.setStrokeOpacity(0);
        _gfx.setStrokeWidth(0);

        std::vector<PointF> points;
        for (const auto& cmd : feature.commands) {
            if (cmd.type == PathCommand::MOVE_TO) {
                if (points.size() >= 2)
                    StrokeToPath::expand(_gfx, points.data(), (int)points.size(), params);
                points.clear();
                points.push_back({ cmd.x, cmd.y });
            } else if (cmd.type == PathCommand::LINE_TO) {
                points.push_back({ cmd.x, cmd.y });
            }
        }
        if (points.size() >= 2)
            StrokeToPath::expand(_gfx, points.data(), (int)points.size(), params);

        _gfx.fillPath();
    }
}

void TileRenderer::drawPoint(const ParsedFeature& feature)
{
    StyleResult sr;
    _style.getStyle(StyleType::POINT, feature.layerName, feature.properties, _zoom, sr);

    _gfx.pathClear();
    _gfx.setFillColor(sr.fill.color);
    _gfx.setFillOpacity(sr.fill.opacity);
    _gfx.setStrokeWidth(0);
    _gfx.setStrokeOpacity(0);

    for (const auto& cmd : feature.commands) {
        if (cmd.type == PathCommand::MOVE_TO)
            _gfx.pathAddCircle(cmd.x, cmd.y, 30, 30);
    }
    _gfx.fillPath();
}
