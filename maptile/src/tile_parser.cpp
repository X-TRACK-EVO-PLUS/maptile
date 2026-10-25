/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile/tile_parser.h"
#include "maptile/style.h"
#include "tile_utils.h"
#include <cstring>

using namespace MapTile;
using namespace vector_tile;

static int findLayerIndex(const Tile& tile, const char* name)
{
    auto size = tile.layers_size();
    for (int i = 0; i < size; i++) {
        if (strcmp(name, tile.layers(i).name().c_str()) == 0) {
            return i;
        }
    }
    return -1;
}

static void parseGeometry(const Tile_Feature& feature, std::vector<PathCommand>& commands)
{
    int cx = 0, cy = 0;
    int i = 0;
    auto size = feature.geometry_size();

    while (i < size) {
        Utils::GEOMETRY_CMD cmd;
        uint32_t count;
        Utils::parseGeometryCmd(feature.geometry(i), &cmd, &count);
        i++;

        switch (cmd) {
        case Utils::GEOMETRY_CMD::MOVE_TO: {
            if (i + 1 >= size) {
                i = size;
                break;
            }
            int x = cx, y = cy;
            Utils::parseNextPoint(feature, i, &x, &y);
            cx = x;
            cy = y;
            PathCommand pc;
            pc.type = PathCommand::MOVE_TO;
            pc.x = (float)x;
            pc.y = (float)y;
            commands.push_back(pc);
            i += 2;
        } break;
        case Utils::GEOMETRY_CMD::LINE_TO: {
            for (uint32_t cnt_i = 0; cnt_i < count && i + 1 < size; cnt_i++) {
                int x = cx, y = cy;
                Utils::parseNextPoint(feature, i, &x, &y);
                cx = x;
                cy = y;
                PathCommand pc;
                pc.type = PathCommand::LINE_TO;
                pc.x = (float)x;
                pc.y = (float)y;
                commands.push_back(pc);
                i += 2;
            }
        } break;
        case Utils::GEOMETRY_CMD::CLOSE_PATH: {
            PathCommand pc;
            pc.type = PathCommand::CLOSE;
            pc.x = 0;
            pc.y = 0;
            commands.push_back(pc);
        } break;
        default:
            break;
        }
    }
}

static ParsedFeature::GeomType convertGeomType(Tile_GeomType type)
{
    switch (type) {
    case Tile_GeomType_POLYGON:
        return ParsedFeature::POLYGON;
    case Tile_GeomType_LINESTRING:
        return ParsedFeature::LINE;
    case Tile_GeomType_POINT:
        return ParsedFeature::POINT;
    default:
        return ParsedFeature::UNKNOWN;
    }
}

ParsedTile TileParser::parse(const Tile& tile, StyleBase& style, float zoom,
    const char** layerSequence, const char** labelSequence)
{
    static const char* defaultLayerSeq[] = {
        "landuse", "water", "structure", "building", "road", "waterway", ""
    };
    static const char* defaultLabelSeq[] = {
        "road", "place_label", "poi_label", "transit_stop_label", ""
    };

    if (!layerSequence)
        layerSequence = defaultLayerSeq;
    if (!labelSequence)
        labelSequence = defaultLabelSeq;

    ParsedTile result;
    result.extent = 4096;

    /* Parse features in layer order */
    for (const char** name = layerSequence; (*name)[0] != '\0'; name++) {
        int layerIndex = findLayerIndex(tile, *name);
        if (layerIndex < 0)
            continue;

        const Tile_Layer& tileLayer = tile.layers(layerIndex);
        result.extent = tileLayer.extent();

        int featureCount = tileLayer.features_size();
        for (int i = 0; i < featureCount; i++) {
            const Tile_Feature& tileFeature = tileLayer.features(i);

            FeatureProp prop;
            Utils::buildTileLayerFeatureProperties(tileLayer, tileFeature, prop);

            if (!style.getShowable(StyleType::FILL, tileLayer.name(), prop, zoom)
                && !style.getShowable(StyleType::STROKE, tileLayer.name(), prop, zoom)) {
                continue;
            }

            ParsedFeature feature;
            feature.geomType = convertGeomType(tileFeature.type());
            feature.layerName = tileLayer.name();
            feature.properties = prop;
            parseGeometry(tileFeature, feature.commands);
            result.features.push_back(feature);
        }
    }

    /* Parse labels in label order */
    for (const char** name = labelSequence; (*name)[0] != '\0'; name++) {
        int layerIndex = findLayerIndex(tile, *name);
        if (layerIndex < 0)
            continue;

        const Tile_Layer& tileLayer = tile.layers(layerIndex);

        int featureCount = tileLayer.features_size();
        for (int i = 0; i < featureCount; i++) {
            const Tile_Feature& tileFeature = tileLayer.features(i);

            FeatureProp prop;
            Utils::buildTileLayerFeatureProperties(tileLayer, tileFeature, prop);

            if (!style.getShowable(StyleType::LABEL, tileLayer.name(), prop, zoom)) {
                continue;
            }

            ParsedFeature label;
            label.geomType = convertGeomType(tileFeature.type());
            label.layerName = tileLayer.name();
            label.properties = prop;
            parseGeometry(tileFeature, label.commands);
            result.labels.push_back(label);
        }
    }

    return result;
}
