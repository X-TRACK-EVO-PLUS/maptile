/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile/mvt.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace MapTile {

namespace {

    struct Reader {
        const uint8_t* p;
        const uint8_t* end;

        bool eof() const { return p >= end; }
        bool varint(uint64_t& v)
        {
            v = 0;
            for (int shift = 0; shift < 64; shift += 7) {
                if (p >= end)
                    return false;
                uint8_t b = *p++;
                v |= uint64_t(b & 0x7f) << shift;
                if (!(b & 0x80))
                    return true;
            }
            return false;
        }
        bool bytes(uint64_t n, Reader& sub)
        {
            if (n > uint64_t(end - p))
                return false;
            sub = { p, p + n };
            p += n;
            return true;
        }
        /* Next field header. `payload` is set for length-delimited fields,
         * `value` for varints and fixed-width ones. */
        bool field(uint32_t& num, uint32_t& wire, uint64_t& value, Reader& payload)
        {
            uint64_t key;
            if (!varint(key))
                return false;
            num = uint32_t(key >> 3);
            wire = uint32_t(key & 7);
            switch (wire) {
            case 0:
                return varint(value);
            case 1:
                if (end - p < 8)
                    return false;
                std::memcpy(&value, p, 8);
                p += 8;
                return true;
            case 2: {
                uint64_t n;
                return varint(n) && bytes(n, payload);
            }
            case 5: {
                if (end - p < 4)
                    return false;
                uint32_t v32;
                std::memcpy(&v32, p, 4);
                value = v32;
                p += 4;
                return true;
            }
            default:
                return false;
            }
        }
    };

    int64_t unzigzag(uint64_t v) { return int64_t(v >> 1) ^ -int64_t(v & 1); }

    std::string numberText(double v)
    {
        char buf[32];
        if (v == (double)(long long)v)
            std::snprintf(buf, sizeof buf, "%lld", (long long)v);
        else
            std::snprintf(buf, sizeof buf, "%g", v);
        return buf;
    }

    bool decodeValue(Reader r, MvtValue& out)
    {
        uint32_t num, wire;
        uint64_t v;
        Reader payload { nullptr, nullptr };
        while (!r.eof()) {
            if (!r.field(num, wire, v, payload))
                return false;
            switch (num) {
            case 1:
                out.kind = MvtValue::STRING;
                out.text.assign((const char*)payload.p, size_t(payload.end - payload.p));
                break;
            case 2: {
                float f;
                uint32_t b = uint32_t(v);
                std::memcpy(&f, &b, 4);
                out.kind = MvtValue::NUMBER;
                out.number = f;
                out.text = numberText(out.number);
                break;
            }
            case 3:
                out.kind = MvtValue::NUMBER;
                std::memcpy(&out.number, &v, 8);
                out.text = numberText(out.number);
                break;
            case 4:
                out.kind = MvtValue::NUMBER;
                out.number = (double)(int64_t)v;
                out.text = numberText(out.number);
                break;
            case 5:
                out.kind = MvtValue::NUMBER;
                out.number = (double)v;
                out.text = numberText(out.number);
                break;
            case 6:
                out.kind = MvtValue::NUMBER;
                out.number = (double)unzigzag(v);
                out.text = numberText(out.number);
                break;
            case 7:
                out.kind = MvtValue::BOOL;
                out.number = v ? 1 : 0;
                out.text = v ? "true" : "false";
                break;
            default:
                break;
            }
        }
        return true;
    }

    int16_t clamp16(int64_t v) { return (int16_t)std::max<int64_t>(-32768, std::min<int64_t>(32767, v)); }

    /* Geometry command stream -> parts. Polygons are rings without the
     * repeated closing point (ClosePath is implied by the feature type). */
    bool decodeGeometry(const std::vector<uint32_t>& g, MvtFeature& f, MvtLayer& layer)
    {
        int64_t x = 0, y = 0;
        size_t i = 0;
        bool open = false;
        auto closePart = [&]() {
            if (open)
                f.partN++;
            open = false;
        };
        auto startPart = [&]() {
            layer.partStart.push_back((uint32_t)layer.points.size());
            open = true;
        };
        auto add = [&](int64_t px, int64_t py) {
            MvtPoint pt { clamp16(px), clamp16(py) };
            layer.points.push_back(pt);
            f.minX = std::min(f.minX, pt.x);
            f.minY = std::min(f.minY, pt.y);
            f.maxX = std::max(f.maxX, pt.x);
            f.maxY = std::max(f.maxY, pt.y);
        };
        f.minX = f.minY = 32767;
        f.maxX = f.maxY = -32768;
        while (i < g.size()) {
            uint32_t cmd = g[i] & 7, count = g[i] >> 3;
            i++;
            if (cmd == 1 || cmd == 2) {
                for (uint32_t n = 0; n < count; n++) {
                    if (i + 2 > g.size())
                        return false;
                    x += unzigzag(g[i]);
                    y += unzigzag(g[i + 1]);
                    i += 2;
                    if (cmd == 1) {
                        /* MoveTo opens a new part; points of a POINT feature each get their own */
                        closePart();
                        startPart();
                    }
                    add(x, y);
                }
            } else if (cmd == 7) {
                closePart();
            } else {
                return false;
            }
        }
        closePart();
        return true;
    }

    bool decodeLayer(Reader r, MvtLayer& layer)
    {
        uint32_t num, wire;
        uint64_t v;
        Reader payload { nullptr, nullptr };
        std::vector<Reader> featureData;
        while (!r.eof()) {
            if (!r.field(num, wire, v, payload))
                return false;
            switch (num) {
            case 1:
                layer.name.assign((const char*)payload.p, size_t(payload.end - payload.p));
                break;
            case 2:
                featureData.push_back(payload);
                break;
            case 3:
                layer.keys.emplace_back((const char*)payload.p, size_t(payload.end - payload.p));
                break;
            case 4: {
                MvtValue val;
                if (!decodeValue(payload, val))
                    return false;
                layer.values.push_back(std::move(val));
                break;
            }
            case 5:
                layer.extent = (uint32_t)v;
                break;
            default:
                break;
            }
        }
        layer.features.reserve(featureData.size());
        for (Reader fr : featureData) {
            MvtFeature f;
            f.part0 = (uint32_t)layer.partStart.size();
            f.tag0 = (uint32_t)layer.tags.size();
            std::vector<uint32_t> geometry;
            while (!fr.eof()) {
                if (!fr.field(num, wire, v, payload))
                    return false;
                if (num == 2 && wire == 2) {
                    Reader t = payload;
                    uint64_t k, val;
                    while (!t.eof()) {
                        if (!t.varint(k) || !t.varint(val))
                            return false;
                        if (k >= layer.keys.size() || val >= layer.values.size())
                            return false;
                        layer.tags.push_back((uint32_t)k);
                        layer.tags.push_back((uint32_t)val);
                    }
                } else if (num == 3 && wire == 0) {
                    f.type = v >= 1 && v <= 3 ? (MvtFeature::Type)v : MvtFeature::UNKNOWN;
                } else if (num == 4 && wire == 2) {
                    Reader g = payload;
                    uint64_t word;
                    while (!g.eof()) {
                        if (!g.varint(word))
                            return false;
                        geometry.push_back((uint32_t)word);
                    }
                }
            }
            f.tagN = (uint32_t)layer.tags.size() - f.tag0;
            f.tagN /= 2;
            f.tag0 /= 2;
            if (!decodeGeometry(geometry, f, layer))
                return false;
            if (f.partN == 0)
                continue;
            layer.features.push_back(f);
        }
        layer.partStart.push_back((uint32_t)layer.points.size());
        return true;
    }

} // namespace

int MvtLayer::keyIndex(const char* key) const
{
    for (size_t i = 0; i < keys.size(); i++)
        if (keys[i] == key)
            return (int)i;
    return -1;
}

int MvtLayer::valueOf(const MvtFeature& f, int keyIdx) const
{
    for (uint32_t i = 0; i < f.tagN; i++) {
        uint32_t t = (f.tag0 + i) * 2;
        if ((int)tags[t] == keyIdx)
            return (int)tags[t + 1];
    }
    return -1;
}

const std::string& MvtLayer::text(const MvtFeature& f, int keyIdx) const
{
    static const std::string none;
    int v = keyIdx < 0 ? -1 : valueOf(f, keyIdx);
    return v < 0 ? none : values[v].text;
}

const MvtLayer* MvtTile::find(const std::string& name) const
{
    for (const auto& l : layers)
        if (l.name == name)
            return &l;
    return nullptr;
}

size_t MvtTile::memoryBytes() const
{
    size_t n = sizeof(*this);
    for (const auto& l : layers) {
        n += l.points.capacity() * sizeof(MvtPoint) + l.features.capacity() * sizeof(MvtFeature)
            + l.partStart.capacity() * 4 + l.tags.capacity() * 4 + l.values.capacity() * sizeof(MvtValue)
            + l.keys.capacity() * sizeof(std::string);
    }
    return n;
}

bool decodeMvt(const uint8_t* data, size_t size, MvtTile& out)
{
    Reader r { data, data + size };
    uint32_t num, wire;
    uint64_t v;
    Reader payload { nullptr, nullptr };
    while (!r.eof()) {
        if (!r.field(num, wire, v, payload))
            return false;
        if (num == 3 && wire == 2) {
            out.layers.emplace_back();
            if (!decodeLayer(payload, out.layers.back())) {
                out.layers.pop_back();
                return false;
            }
        }
    }
    return true;
}

} // namespace MapTile
