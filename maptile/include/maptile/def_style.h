/*
 * MIT License
 * Copyright (c) 2024 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_DEF_STYLE_H
#define MAPTILE_DEF_STYLE_H

#include "maptile/style.h"

namespace MapTile {

class DefStyle : public StyleBase {
public:
    bool getShowable(StyleType type, const std::string& layerName,
        const FeatureProp& prop, float zoom) override;
    bool getStyle(StyleType type, const std::string& layerName,
        const FeatureProp& prop, float zoom, StyleResult& result) override;

    static const char** layerSequence();
    static const char** labelSequence();
};

} // namespace MapTile

#endif // MAPTILE_DEF_STYLE_H
