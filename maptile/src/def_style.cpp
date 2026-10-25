/*
 * MIT License
 * Copyright (c) 2024 HanfG, _VIFEXTech
 */
#include "maptile/def_style.h"

using namespace MapTile;

static const char* _layerSeq[] = {
    "land", "water_polygons", "water_lines",
    "sites", "buildings", "streets", "bridges",
    ""
};

static const char* _labelSeq[] = {
    "street_labels", "place_labels", "pois",
    ""
};

const char** DefStyle::layerSequence() { return _layerSeq; }
const char** DefStyle::labelSequence() { return _labelSeq; }

bool DefStyle::getShowable(StyleType type, const std::string& layerName,
    const FeatureProp& prop, float zoom)
{
    (void)prop;
    (void)zoom;

    switch (type) {
    case StyleType::LABEL:
        return (layerName == "street_labels"
            || layerName == "place_labels"
            || layerName == "pois");
    default:
        return true;
    }
}

static std::string getPropString(const FeatureProp& prop, const std::string& key)
{
    auto it = prop.find(key);
    if (it == prop.end())
        return "";
    return it->second.string_value();
}

bool DefStyle::getStyle(StyleType type, const std::string& layerName,
    const FeatureProp& prop, float /*zoom*/, StyleResult& result)
{
    switch (type) {

    case StyleType::BACKGROUND:
        result.fill.color = Color(0xE8, 0xE0, 0xD8);
        result.fill.opacity = 255;
        return true;

    case StyleType::FILL: {
        result.fill.opacity = 255;
        if (layerName == "water_polygons") {
            result.fill.color = Color::fromHex(0xAAD3DF);
            return true;
        }
        if (layerName == "land") {
            auto kind = getPropString(prop, "kind");
            if (kind == "park" || kind == "garden" || kind == "grass" || kind == "forest") {
                result.fill.color = Color::fromHex(0xCDEBB0);
                return true;
            }
            if (kind == "hospital") {
                result.fill.color = Color::fromHex(0xF3D3D3);
                return true;
            }
            if (kind == "school" || kind == "university" || kind == "college") {
                result.fill.color = Color::fromHex(0xEAD6C8);
                return true;
            }
            if (kind == "residential") {
                result.fill.color = Color::fromHex(0xE0DFDF);
                return true;
            }
            if (kind == "commercial" || kind == "retail") {
                result.fill.color = Color::fromHex(0xF2DAD9);
                return true;
            }
            if (kind == "industrial") {
                result.fill.color = Color::fromHex(0xEBDCE8);
                return true;
            }
            result.fill.opacity = 0;
            return true;
        }
        if (layerName == "buildings") {
            result.fill.color = Color::fromHex(0xD9D0C9);
            return true;
        }
        if (layerName == "sites") {
            result.fill.color = Color::fromHex(0xE0DFDF);
            result.fill.opacity = 128;
            return true;
        }
        result.fill.opacity = 0;
        return true;
    }

    case StyleType::STROKE: {
        result.stroke.opacity = 255;
        if (layerName == "water_lines") {
            result.stroke.color = Color::fromHex(0xAAD3DF);
            result.stroke.width = 1.5f;
            return true;
        }
        if (layerName == "streets" || layerName == "bridges") {
            auto kind = getPropString(prop, "kind");
            if (kind == "motorway" || kind == "trunk") {
                result.stroke.color = Color::fromHex(0xFFA35C);
                result.stroke.width = 3.0f;
            } else if (kind == "primary") {
                result.stroke.color = Color::fromHex(0xFFD88A);
                result.stroke.width = 2.5f;
            } else if (kind == "secondary" || kind == "tertiary") {
                result.stroke.color = Color::fromHex(0xFFFFFF);
                result.stroke.width = 2.0f;
            } else if (kind == "residential" || kind == "living_street" || kind == "unclassified") {
                result.stroke.color = Color::fromHex(0xFFFFFF);
                result.stroke.width = 1.2f;
            } else if (kind == "service" || kind == "track" || kind == "path"
                || kind == "footway" || kind == "cycleway") {
                result.stroke.color = Color::fromHex(0xEDEDED);
                result.stroke.width = 0.8f;
            } else {
                result.stroke.color = Color::fromHex(0xFFFFFF);
                result.stroke.width = 1.0f;
            }
            return true;
        }
        result.stroke.opacity = 0;
        return true;
    }

    case StyleType::POINT:
        result.fill.color = Color::fromHex(0x7B68A0);
        result.fill.opacity = 255;
        return true;

    case StyleType::LABEL: {
        result.label.color = Color::fromHex(0x333333);
        return true;
    }
    }
    return false;
}
