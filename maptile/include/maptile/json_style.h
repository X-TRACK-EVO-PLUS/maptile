/*
 * MIT License
 * Copyright (c) 2024 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_JSON_STYLE_H
#define MAPTILE_JSON_STYLE_H

#include "maptile/style.h"
#include <string>
#include <vector>

namespace MapTile {

class JsonStyle : public StyleBase {
public:
    JsonStyle();
    ~JsonStyle();

    bool loadFromFile(const char* path);
    bool loadFromString(const char* json);

    bool getShowable(StyleType type, const std::string& layerName,
        const FeatureProp& prop, float zoom) override;
    bool getStyle(StyleType type, const std::string& layerName,
        const FeatureProp& prop, float zoom, StyleResult& result) override;

    const char** layerSequence() const;
    const char** labelSequence() const;
    const std::string& getName() const { return _name; }

    struct StyleEntry {
        Color color;
        float width;
        uint8_t opacity;
        float minZoom;
        std::string icon;
        bool hasColor;
        bool hasWidth;
        bool hasOpacity;
        bool hasMinZoom;

        StyleEntry()
            : width(1.0f)
            , opacity(255)
            , minZoom(0)
            , hasColor(false)
            , hasWidth(false)
            , hasOpacity(false)
            , hasMinZoom(false)
        {
        }
    };

    struct KV {
        std::string key;
        StyleEntry value;
    };

    struct MatchRule {
        std::string key;
        std::vector<KV> values;
        StyleEntry defaultEntry;
    };

    struct LayerDef {
        std::string name;
        MatchRule fill;
        MatchRule stroke;
        MatchRule label;
        MatchRule show;
        bool hasFill = false;
        bool hasStroke = false;
        bool hasLabel = false;
        bool hasShow = false;
    };

    bool parse(const char* json);

private:
    static Color parseColor(const char* str);
    static StyleEntry parseStyleEntry(void* jsonObj);
    static MatchRule parseMatchRule(void* jsonObj);

    const LayerDef* findLayer(const std::string& name) const;
    static const StyleEntry* findMatch(const MatchRule& rule, const FeatureProp& prop);

    std::string _name;
    FillStyle _background;
    std::vector<LayerDef> _layers;
    std::vector<std::string> _renderOrder;
    std::vector<std::string> _labelOrder;
    mutable std::vector<const char*> _renderOrderC;
    mutable std::vector<const char*> _labelOrderC;
};

} // namespace MapTile

#endif // MAPTILE_JSON_STYLE_H
