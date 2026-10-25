/*
 * MIT License
 * Copyright (c) 2024 HanfG, _VIFEXTech
 */
#include "maptile/json_style.h"
#include <cJSON.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace MapTile;

JsonStyle::JsonStyle() { _background.color = Color(0xE8, 0xE0, 0xD8); }
JsonStyle::~JsonStyle() { }

Color JsonStyle::parseColor(const char* str)
{
    if (!str || str[0] != '#')
        return Color();
    unsigned int hex = (unsigned int)strtoul(str + 1, nullptr, 16);
    int len = (int)strlen(str + 1);
    if (len == 8)
        return Color((hex >> 24) & 0xFF, (hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
    return Color::fromHex(hex);
}

JsonStyle::StyleEntry JsonStyle::parseStyleEntry(void* obj)
{
    cJSON* j = (cJSON*)obj;
    StyleEntry e;
    cJSON* c;
    if ((c = cJSON_GetObjectItem(j, "color")) && cJSON_IsString(c)) {
        e.color = parseColor(c->valuestring);
        e.hasColor = true;
    }
    if ((c = cJSON_GetObjectItem(j, "width")) && cJSON_IsNumber(c)) {
        e.width = (float)c->valuedouble;
        e.hasWidth = true;
    }
    if ((c = cJSON_GetObjectItem(j, "opacity")) && cJSON_IsNumber(c)) {
        e.opacity = (uint8_t)(c->valuedouble * 255);
        e.hasOpacity = true;
    }
    if ((c = cJSON_GetObjectItem(j, "min_zoom")) && cJSON_IsNumber(c)) {
        e.minZoom = (float)c->valuedouble;
        e.hasMinZoom = true;
    }
    if ((c = cJSON_GetObjectItem(j, "icon")) && cJSON_IsString(c)) {
        e.icon = c->valuestring;
    }
    return e;
}

JsonStyle::MatchRule JsonStyle::parseMatchRule(void* obj)
{
    cJSON* j = (cJSON*)obj;
    MatchRule rule;

    cJSON* def = cJSON_GetObjectItem(j, "default");
    if (def && cJSON_IsObject(def))
        rule.defaultEntry = parseStyleEntry(def);

    cJSON* match = cJSON_GetObjectItem(j, "match");
    if (!match) {
        rule.defaultEntry = parseStyleEntry(j);
        return rule;
    }

    cJSON* key = cJSON_GetObjectItem(match, "key");
    if (key && cJSON_IsString(key))
        rule.key = key->valuestring;

    cJSON* values = cJSON_GetObjectItem(match, "values");
    if (values && cJSON_IsObject(values)) {
        for (cJSON* item = values->child; item; item = item->next) {
            if (cJSON_IsObject(item)) {
                KV kv;
                kv.key = item->string;
                kv.value = parseStyleEntry(item);
                rule.values.push_back(kv);
            }
        }
    }

    cJSON* icons = cJSON_GetObjectItem(j, "icons");
    if (icons && cJSON_IsObject(icons)) {
        cJSON* ikey = cJSON_GetObjectItem(icons, "key");
        if (ikey && cJSON_IsString(ikey))
            rule.key = ikey->valuestring;
        cJSON* ivalues = cJSON_GetObjectItem(icons, "values");
        if (ivalues && cJSON_IsObject(ivalues)) {
            for (cJSON* item = ivalues->child; item; item = item->next) {
                if (cJSON_IsString(item)) {
                    /* Find existing or add new */
                    bool found = false;
                    for (auto& kv : rule.values) {
                        if (kv.key == item->string) {
                            kv.value.icon = item->valuestring;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        KV kv;
                        kv.key = item->string;
                        kv.value.icon = item->valuestring;
                        rule.values.push_back(kv);
                    }
                }
            }
        }
    }
    return rule;
}

bool JsonStyle::parse(const char* json)
{
    cJSON* root = cJSON_Parse(json);
    if (!root)
        return false;

    cJSON* name = cJSON_GetObjectItem(root, "name");
    if (name && cJSON_IsString(name))
        _name = name->valuestring;

    cJSON* bg = cJSON_GetObjectItem(root, "background");
    if (bg) {
        cJSON* c = cJSON_GetObjectItem(bg, "color");
        if (c && cJSON_IsString(c))
            _background.color = parseColor(c->valuestring);
    }

    cJSON* ro = cJSON_GetObjectItem(root, "render_order");
    if (ro && cJSON_IsArray(ro)) {
        _renderOrder.clear();
        cJSON* item;
        cJSON_ArrayForEach(item, ro)
        {
            if (cJSON_IsString(item))
                _renderOrder.push_back(item->valuestring);
        }
    }

    cJSON* lo = cJSON_GetObjectItem(root, "label_order");
    if (lo && cJSON_IsArray(lo)) {
        _labelOrder.clear();
        cJSON* item;
        cJSON_ArrayForEach(item, lo)
        {
            if (cJSON_IsString(item))
                _labelOrder.push_back(item->valuestring);
        }
    }

    cJSON* layers = cJSON_GetObjectItem(root, "layers");
    if (layers && cJSON_IsObject(layers)) {
        for (cJSON* layer = layers->child; layer; layer = layer->next) {
            if (!cJSON_IsObject(layer))
                continue;
            LayerDef def;
            def.name = layer->string;
            cJSON* fill = cJSON_GetObjectItem(layer, "fill");
            if (fill) {
                def.fill = parseMatchRule(fill);
                def.hasFill = true;
            }
            cJSON* stroke = cJSON_GetObjectItem(layer, "stroke");
            if (stroke) {
                def.stroke = parseMatchRule(stroke);
                def.hasStroke = true;
            }
            cJSON* label = cJSON_GetObjectItem(layer, "label");
            if (label) {
                def.label = parseMatchRule(label);
                def.hasLabel = true;
            }
            cJSON* show = cJSON_GetObjectItem(layer, "show");
            if (show) {
                def.show = parseMatchRule(show);
                def.hasShow = true;
            }
            _layers.push_back(def);
        }
    }

    cJSON_Delete(root);
    return true;
}

bool JsonStyle::loadFromString(const char* json) { return parse(json); }

bool JsonStyle::loadFromFile(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(size + 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    bool ok = parse(buf);
    free(buf);
    return ok;
}

const char** JsonStyle::layerSequence() const
{
    _renderOrderC.clear();
    for (const auto& s : _renderOrder)
        _renderOrderC.push_back(s.c_str());
    _renderOrderC.push_back("");
    return _renderOrderC.data();
}

const char** JsonStyle::labelSequence() const
{
    _labelOrderC.clear();
    for (const auto& s : _labelOrder)
        _labelOrderC.push_back(s.c_str());
    _labelOrderC.push_back("");
    return _labelOrderC.data();
}

const JsonStyle::LayerDef* JsonStyle::findLayer(const std::string& name) const
{
    for (const auto& l : _layers) {
        if (l.name == name)
            return &l;
    }
    return nullptr;
}

const JsonStyle::StyleEntry* JsonStyle::findMatch(const MatchRule& rule, const FeatureProp& prop)
{
    if (!rule.key.empty()) {
        auto it = prop.find(rule.key);
        std::string val = (it != prop.end()) ? it->second.string_value() : "";
        for (const auto& kv : rule.values) {
            if (kv.key == val)
                return &kv.value;
        }
        for (const auto& kv : rule.values) {
            if (kv.key == "default")
                return &kv.value;
        }
    }
    return &rule.defaultEntry;
}

bool JsonStyle::getShowable(StyleType /*type*/, const std::string& layerName,
    const FeatureProp& prop, float zoom)
{
    const LayerDef* def = findLayer(layerName);
    if (!def)
        return true;
    if (!def->hasShow)
        return true;
    const StyleEntry* entry = findMatch(def->show, prop);
    if (entry && entry->hasMinZoom && zoom < entry->minZoom)
        return false;
    return true;
}

bool JsonStyle::getStyle(StyleType type, const std::string& layerName,
    const FeatureProp& prop, float /*zoom*/, StyleResult& result)
{
    if (type == StyleType::BACKGROUND) {
        result.fill = _background;
        return true;
    }

    const LayerDef* def = findLayer(layerName);
    if (!def)
        return false;

    switch (type) {
    case StyleType::FILL: {
        if (!def->hasFill) {
            result.fill.opacity = 0;
            return true;
        }
        const StyleEntry* e = findMatch(def->fill, prop);
        if (e) {
            if (e->hasColor)
                result.fill.color = e->color;
            result.fill.opacity = e->hasOpacity ? e->opacity : 255;
        } else {
            result.fill.opacity = 0;
        }
        return true;
    }
    case StyleType::STROKE: {
        if (!def->hasStroke) {
            result.stroke.opacity = 0;
            return true;
        }
        const StyleEntry* e = findMatch(def->stroke, prop);
        if (e) {
            if (e->hasColor)
                result.stroke.color = e->color;
            if (e->hasWidth)
                result.stroke.width = e->width;
            result.stroke.opacity = e->hasOpacity ? e->opacity : 255;
        } else {
            result.stroke.opacity = 0;
        }
        return true;
    }
    case StyleType::POINT: {
        const StyleEntry* e = findMatch(def->fill, prop);
        if (e && e->hasColor) {
            result.fill.color = e->color;
            result.fill.opacity = e->hasOpacity ? e->opacity : 255;
        }
        return true;
    }
    case StyleType::LABEL: {
        if (!def->hasLabel)
            return false;
        const StyleEntry* e = findMatch(def->label, prop);
        if (e) {
            if (e->hasColor)
                result.label.color = e->color;
            if (!e->icon.empty())
                result.label.icon = e->icon.c_str();
        }
        return true;
    }
    default:
        break;
    }
    return false;
}
