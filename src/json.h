/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 *
 * Minimal JSON reader for style files. Internal: not installed, not exported.
 */
#ifndef MAPTILE_JSON_H
#define MAPTILE_JSON_H
#include <string>
#include <utility>
#include <vector>

namespace MapTile {

struct Json {
    enum Type { NUL,
        BOOL,
        NUM,
        STR,
        ARR,
        OBJ };
    Type type = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    const Json* get(const char* key) const
    {
        for (const auto& kv : o)
            if (kv.first == key)
                return &kv.second;
        return nullptr;
    }
    bool isNum() const { return type == NUM; }
    bool isStr() const { return type == STR; }
    bool isArr() const { return type == ARR; }
    bool isObj() const { return type == OBJ; }
};

/* On failure returns false and describes the problem in `error`. */
bool parseJson(const char* text, size_t size, Json& out, std::string& error);

} // namespace MapTile

#endif // MAPTILE_JSON_H
