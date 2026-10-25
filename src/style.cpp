/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile/style.h"
#include "json.h"
#include <cstdio>
#include <cstring>

namespace MapTile {

float Stops::at(float zoom) const
{
    if (stops.empty())
        return constant;
    if (zoom <= stops.front().first)
        return stops.front().second;
    if (zoom >= stops.back().first)
        return stops.back().second;
    for (size_t i = 1; i < stops.size(); i++) {
        if (zoom <= stops[i].first) {
            float z0 = stops[i - 1].first, z1 = stops[i].first;
            float t = z1 > z0 ? (zoom - z0) / (z1 - z0) : 0;
            return stops[i - 1].second + (stops[i].second - stops[i - 1].second) * t;
        }
    }
    return stops.back().second;
}

namespace {

    struct Loader {
        std::string err;
        std::map<std::string, Color>& palette;

        explicit Loader(std::map<std::string, Color>& p)
            : palette(p)
        {
        }

        bool fail(const std::string& what)
        {
            if (err.empty())
                err = what;
            return false;
        }

        bool colorRef(const Json* j, const std::string& where, ColorRef& out)
        {
            if (!j)
                return true;
            if (!j->isStr())
                return fail(where + ": colour must be a string");
            if (!j->s.empty() && j->s[0] == '$') {
                out.name = j->s.substr(1);
                return true;
            }
            out.name.clear();
            if (!Color::parse(j->s.c_str(), out.value))
                return fail(where + ": bad colour '" + j->s + "'");
            return true;
        }

        bool stops(const Json* j, const std::string& where, Stops& out)
        {
            if (!j)
                return true;
            if (j->isNum()) {
                out.constant = (float)j->n;
                out.stops.clear();
                return true;
            }
            if (!j->isArr())
                return fail(where + ": expected a number or [[zoom, value], ...]");
            out.stops.clear();
            for (const Json& s : j->a) {
                if (!s.isArr() || s.a.size() != 2 || !s.a[0].isNum() || !s.a[1].isNum())
                    return fail(where + ": stops are [zoom, value] pairs");
                out.stops.emplace_back((float)s.a[0].n, (float)s.a[1].n);
            }
            return true;
        }

        bool filter(const Json* j, const std::string& where, Filter& out)
        {
            if (!j)
                return true;
            if (!j->isObj())
                return fail(where + ": filter must be an object");
            for (const auto& kv : j->o) {
                Filter::Term t;
                t.key = kv.first;
                if (!t.key.empty() && t.key[0] == '!') {
                    t.negate = true;
                    t.key.erase(0, 1);
                }
                const Json& v = kv.second;
                if (v.isArr()) {
                    for (const Json& e : v.a)
                        t.values.push_back(e.isStr() ? e.s : text(e));
                } else {
                    t.values.push_back(v.isStr() ? v.s : text(v));
                }
                out.terms.push_back(std::move(t));
            }
            return true;
        }

        static std::string text(const Json& j)
        {
            char buf[32];
            if (j.type == Json::BOOL)
                return j.b ? "true" : "false";
            if (j.type == Json::NUM) {
                if (j.n == (double)(long long)j.n)
                    std::snprintf(buf, sizeof buf, "%lld", (long long)j.n);
                else
                    std::snprintf(buf, sizeof buf, "%g", j.n);
                return buf;
            }
            return std::string();
        }

        void range(const Json& j, float& lo, float& hi)
        {
            if (const Json* v = j.get("min_zoom"))
                lo = (float)v->n;
            if (const Json* v = j.get("max_zoom"))
                hi = (float)v->n;
        }
    };

} // namespace

bool Style::loadFile(const char* path)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        _error = std::string("cannot open ") + path;
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::string text(n > 0 ? (size_t)n : 0, '\0');
    bool ok = n > 0 && std::fread(&text[0], 1, text.size(), f) == text.size();
    std::fclose(f);
    if (!ok) {
        _error = std::string("cannot read ") + path;
        return false;
    }
    return loadString(text);
}

bool Style::loadString(const char* text, size_t size)
{
    Json root;
    std::string err;
    if (!parseJson(text, size, root, err)) {
        _error = "style JSON: " + err;
        return false;
    }
    if (!root.isObj()) {
        _error = "style JSON: top level must be an object";
        return false;
    }
    const Json* schema = root.get("schema");
    if (!schema || !schema->isNum() || (int)schema->n != 2) {
        _error = "style: \"schema\": 2 is required";
        return false;
    }

    std::map<std::string, Color> palette = _palette; /* keep overrides made before loading */
    Loader ld(palette);
    std::string name;
    ColorRef bg;
    bg.value = { 0xee, 0xee, 0xee, 255 };
    std::vector<StyleLayer> layers;
    std::vector<StyleLabel> labels;

    if (const Json* n = root.get("name"))
        name = n->s;
    if (const Json* p = root.get("palette")) {
        if (!p->isObj())
            return (_error = "style: palette must be an object"), false;
        for (const auto& kv : p->o) {
            Color c;
            if (!kv.second.isStr() || !Color::parse(kv.second.s.c_str(), c))
                return (_error = "style: bad palette colour '" + kv.first + "'"), false;
            palette.insert(std::make_pair(kv.first, c)); /* an existing override wins */
        }
    }
    if (!ld.colorRef(root.get("background"), "background", bg))
        return (_error = ld.err), false;

    if (const Json* ls = root.get("layers")) {
        if (!ls->isArr())
            return (_error = "style: layers must be an array"), false;
        for (const Json& j : ls->a) {
            StyleLayer l;
            std::string where = "layer " + (j.get("id") ? j.get("id")->s : std::string("?"));
            if (!j.isObj() || !j.get("source") || !j.get("type"))
                return (_error = where + ": \"source\" and \"type\" are required"), false;
            if (const Json* v = j.get("id"))
                l.id = v->s;
            l.source = j.get("source")->s;
            const std::string& type = j.get("type")->s;
            if (type == "fill")
                l.kind = StyleLayer::FILL;
            else if (type == "line")
                l.kind = StyleLayer::LINE;
            else
                return (_error = where + ": type must be fill or line"), false;
            ld.range(j, l.minZoom, l.maxZoom);
            if (!ld.colorRef(j.get("color"), where, l.color) || !ld.stops(j.get("width"), where, l.width)
                || !ld.stops(j.get("opacity"), where, l.opacity) || !ld.filter(j.get("filter"), where, l.filter))
                return (_error = ld.err), false;
            if (const Json* v = j.get("cap")) {
                l.cap = v->s == "butt" ? Cap::BUTT : v->s == "square" ? Cap::SQUARE
                                                                      : Cap::ROUND;
            }
            if (const Json* v = j.get("join")) {
                l.join = v->s == "miter" ? Join::MITER : v->s == "bevel" ? Join::BEVEL
                                                                         : Join::ROUND;
            }
            if (const Json* v = j.get("dash")) {
                for (const Json& d : v->a)
                    l.dash.push_back((float)d.n);
            }
            layers.push_back(std::move(l));
        }
    }
    if (const Json* ls = root.get("labels")) {
        if (!ls->isArr())
            return (_error = "style: labels must be an array"), false;
        for (const Json& j : ls->a) {
            StyleLabel l;
            std::string where = "label " + (j.get("id") ? j.get("id")->s : std::string("?"));
            if (!j.isObj() || !j.get("source"))
                return (_error = where + ": \"source\" is required"), false;
            if (const Json* v = j.get("id"))
                l.id = v->s;
            l.source = j.get("source")->s;
            if (const Json* v = j.get("field"))
                l.field = v->s;
            if (const Json* v = j.get("weight"))
                l.weight = (int)v->n;
            if (const Json* v = j.get("repeat"))
                l.repeat = (float)v->n;
            l.size.constant = 11;
            l.halo.value = { 0, 0, 0, 0 };
            ld.range(j, l.minZoom, l.maxZoom);
            if (!ld.colorRef(j.get("color"), where, l.color) || !ld.colorRef(j.get("halo"), where, l.halo)
                || !ld.stops(j.get("size"), where, l.size) || !ld.filter(j.get("filter"), where, l.filter))
                return (_error = ld.err), false;
            labels.push_back(std::move(l));
        }
    }

    _name = name;
    _palette = std::move(palette);
    _background = bg;
    _layers = std::move(layers);
    _labels = std::move(labels);
    _error.clear();
    resolve();
    return true;
}

void Style::resolve(ColorRef& ref)
{
    if (ref.name.empty())
        return;
    auto it = _palette.find(ref.name);
    if (it != _palette.end())
        ref.value = it->second;
}

void Style::resolve()
{
    resolve(_background);
    for (auto& l : _layers)
        resolve(l.color);
    for (auto& l : _labels) {
        resolve(l.color);
        resolve(l.halo);
    }
}

void Style::setColor(const std::string& name, const Color& color)
{
    _palette[name] = color;
    resolve();
}

bool Style::color(const std::string& name, Color& out) const
{
    auto it = _palette.find(name);
    if (it == _palette.end())
        return false;
    out = it->second;
    return true;
}

} // namespace MapTile
