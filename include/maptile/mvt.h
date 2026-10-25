/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_MVT_H
#define MAPTILE_MVT_H
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace MapTile {

/*
 * Decoded Mapbox Vector Tile (spec 2.1), stored flat: one coordinate array
 * per layer, features as index ranges into it. No protobuf library is used.
 */
struct MvtValue {
    enum Kind : uint8_t { NONE,
        STRING,
        NUMBER,
        BOOL };
    Kind kind = NONE;
    double number = 0;
    std::string text; /* STRING, and the textual form of numbers and bools */
};

struct MvtPoint {
    int16_t x, y;
};

struct MvtFeature {
    enum Type : uint8_t { UNKNOWN = 0,
        POINT = 1,
        LINE = 2,
        POLYGON = 3 };
    Type type = UNKNOWN;
    uint32_t part0 = 0; /* first part (a point cluster, a line, or a ring) */
    uint32_t partN = 0;
    uint32_t tag0 = 0; /* index into MvtLayer::tags, in (key, value) pairs */
    uint32_t tagN = 0;
    int16_t minX = 0, minY = 0, maxX = 0, maxY = 0; /* bounds in tile units */
};

struct MvtLayer {
    std::string name;
    uint32_t extent = 4096;
    std::vector<std::string> keys;
    std::vector<MvtValue> values;
    std::vector<MvtFeature> features;
    std::vector<MvtPoint> points; /* all parts, back to back */
    std::vector<uint32_t> partStart; /* parts + 1 entries, offsets into points */
    std::vector<uint32_t> tags; /* key index, value index, ... */

    int keyIndex(const char* key) const;
    /* Value index of `keyIdx` on `f`, or -1 */
    int valueOf(const MvtFeature& f, int keyIdx) const;
    const std::string& text(const MvtFeature& f, int keyIdx) const;
    uint32_t partBegin(uint32_t part) const { return partStart[part]; }
    uint32_t partEnd(uint32_t part) const { return partStart[part + 1]; }
};

struct MvtTile {
    std::vector<MvtLayer> layers;
    const MvtLayer* find(const std::string& name) const;
    size_t memoryBytes() const;
};

/* Returns false on malformed input; `out` then holds what was decoded so far. */
bool decodeMvt(const uint8_t* data, size_t size, MvtTile& out);

} // namespace MapTile

#endif // MAPTILE_MVT_H
