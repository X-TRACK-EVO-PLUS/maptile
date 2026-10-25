/*
 * MIT License
 * Copyright (c) 2024 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_STYLE_H
#define MAPTILE_STYLE_H

#include "maptile/types.h"
#include <string>

namespace MapTile {

enum class StyleType {
    BACKGROUND,
    FILL,
    STROKE,
    POINT,
    LABEL,
};

struct StyleResult {
    FillStyle fill;
    StrokeStyle stroke;
    LabelStyle label;
};

class StyleBase {
public:
    virtual ~StyleBase() = default;

    virtual bool getShowable(StyleType /*type*/, const std::string& /*layerName*/,
        const FeatureProp& /*prop*/, float /*zoom*/)
    {
        return false;
    }

    virtual bool getStyle(StyleType /*type*/, const std::string& /*layerName*/,
        const FeatureProp& /*prop*/, float /*zoom*/, StyleResult& /*result*/)
    {
        return false;
    }
};

} // namespace MapTile

#endif // MAPTILE_STYLE_H
