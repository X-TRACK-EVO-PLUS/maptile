/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_STYLE_H
#define MAPTILE_STYLE_H
#include "maptile/canvas.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace MapTile {

/* A value that follows the zoom: `[[zoom, value], ...]` interpolated linearly,
 * or a single number. */
struct Stops {
    std::vector<std::pair<float, float>> stops;
    float constant = 1.0f;

    float at(float zoom) const;
};

/* Colour as written in the style: a literal, or "$name" looked up in the palette. */
struct ColorRef {
    std::string name;
    Color value;
};

/* feature properties must match every term; a term matches when the property
 * text is one of `values` (or, with `negate`, is none of them / missing) */
struct Filter {
    struct Term {
        std::string key;
        bool negate = false;
        std::vector<std::string> values;
    };
    std::vector<Term> terms;
};

struct StyleLayer {
    enum Kind { FILL,
        LINE };
    std::string id;
    std::string source; /* tile layer name */
    Kind kind = FILL;
    Filter filter;
    ColorRef color;
    Stops width; /* LINE only, pixels */
    Stops opacity;
    float minZoom = 0, maxZoom = 99;
    Cap cap = Cap::ROUND;
    Join join = Join::ROUND;
    std::vector<float> dash;
};

struct StyleLabel {
    std::string id;
    std::string source;
    Filter filter;
    std::string field = "name";
    ColorRef color;
    ColorRef halo;
    Stops size;
    int weight = 500;
    float minZoom = 0, maxZoom = 99;
    float repeat = 220; /* pixels between repeats of a line label */
};

/*
 * A map style. Layers are drawn in array order, labels in array order with
 * earlier ones winning collisions. Colours that start with '$' come from the
 * palette, so one style serves several themes: override with setColor().
 */
class Style {
public:
    bool loadFile(const char* path);
    bool loadString(const char* json, size_t size);
    bool loadString(const std::string& json) { return loadString(json.data(), json.size()); }
    const std::string& error() const { return _error; }

    const std::string& name() const { return _name; }
    Color background() const { return _background.value; }
    const std::vector<StyleLayer>& layers() const { return _layers; }
    const std::vector<StyleLabel>& labels() const { return _labels; }

    /* Palette entries as loaded, plus any override. Takes effect immediately. */
    void setColor(const std::string& name, const Color& color);
    bool color(const std::string& name, Color& out) const;
    const std::map<std::string, Color>& palette() const { return _palette; }

private:
    void resolve();
    void resolve(ColorRef& ref);

    std::string _name;
    std::string _error;
    ColorRef _background;
    std::map<std::string, Color> _palette;
    std::vector<StyleLayer> _layers;
    std::vector<StyleLabel> _labels;
};

} // namespace MapTile

#endif // MAPTILE_STYLE_H
