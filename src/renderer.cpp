/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile/renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace MapTile {

namespace {

    uint64_t tileKey(int z, int x, int y) { return (uint64_t(z) << 52) | (uint64_t(x) << 26) | uint64_t(y); }

    /* A style filter bound to one tile layer: each value of the layer is
     * tested once, so matching a feature is a few array lookups. */
    struct FilterMatcher {
        struct Term {
            int key;
            bool negate;
            std::vector<uint8_t> ok; /* per value index */
        };
        std::vector<Term> terms;
        bool impossible = false;

        FilterMatcher(const MvtLayer& layer, const Filter& filter)
        {
            for (const auto& t : filter.terms) {
                int key = layer.keyIndex(t.key.c_str());
                if (key < 0) {
                    if (!t.negate)
                        impossible = true; /* nothing here has this property */
                    continue;
                }
                Term term { key, t.negate, std::vector<uint8_t>(layer.values.size(), 0) };
                for (size_t i = 0; i < layer.values.size(); i++)
                    term.ok[i] = std::find(t.values.begin(), t.values.end(), layer.values[i].text) != t.values.end();
                terms.push_back(std::move(term));
            }
        }
        bool operator()(const MvtLayer& layer, const MvtFeature& f) const
        {
            for (const auto& t : terms) {
                int v = layer.valueOf(f, t.key);
                bool in = v >= 0 && t.ok[v];
                if (in == t.negate)
                    return false;
            }
            return true;
        }
    };

    Rect transformedBounds(const Matrix& m, float x0, float y0, float x1, float y1)
    {
        Vec2 c[4] = { m.apply({ x0, y0 }), m.apply({ x1, y0 }), m.apply({ x1, y1 }), m.apply({ x0, y1 }) };
        Rect r { c[0].x, c[0].y, c[0].x, c[0].y };
        for (int i = 1; i < 4; i++) {
            r.x0 = std::min(r.x0, c[i].x);
            r.y0 = std::min(r.y0, c[i].y);
            r.x1 = std::max(r.x1, c[i].x);
            r.y1 = std::max(r.y1, c[i].y);
        }
        return r;
    }

    /* Project one part into `batch`, dropping vertices closer than the
     * tolerance to the previous one. Returns false (and adds nothing) if too
     * little is left to draw. */
    bool emitPart(PathBatch& batch, const MvtLayer& layer, uint32_t part, const Matrix& m, float tolSq, bool ring,
        float minExtent)
    {
        uint32_t a = layer.partBegin(part), b = layer.partEnd(part);
        batch.begin();
        Vec2 last {}, v {};
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        bool dropped = false;
        for (uint32_t i = a; i < b; i++) {
            v = m.apply({ (float)layer.points[i].x, (float)layer.points[i].y });
            if (i == a || (v.x - last.x) * (v.x - last.x) + (v.y - last.y) * (v.y - last.y) >= tolSq) {
                batch.add(v);
                last = v;
                dropped = false;
                minX = std::min(minX, v.x);
                minY = std::min(minY, v.y);
                maxX = std::max(maxX, v.x);
                maxY = std::max(maxY, v.y);
            } else {
                dropped = true;
            }
        }
        if (dropped && !ring) {
            batch.add(v); /* a line keeps its true end point */
            minX = std::min(minX, v.x);
            minY = std::min(minY, v.y);
            maxX = std::max(maxX, v.x);
            maxY = std::max(maxY, v.y);
        }
        uint32_t n = batch.count();
        bool tiny = (maxX - minX) < minExtent && (maxY - minY) < minExtent;
        if (n < (ring ? 3u : 2u) || (ring && tiny)) {
            batch.rollback();
            return false;
        }
        batch.end(ring);
        return true;
    }

} // namespace

MapRenderer::MapRenderer(TileSource& source, Style& style)
    : _source(source)
    , _style(style)
{
}

void MapRenderer::clearCache()
{
    _cache.clear();
    _lru.clear();
}

int MapRenderer::tileLevel(float zoom) const
{
    int z = (int)std::floor(zoom + 0.5f);
    return std::max(_source.minZoom(), std::min(_source.maxZoom(), z));
}

std::shared_ptr<const MvtTile> MapRenderer::loadTile(int z, int x, int y, bool& decoded)
{
    uint64_t key = tileKey(z, x, y);
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        _lru.splice(_lru.begin(), _lru, it->second.lru);
        return it->second.tile;
    }
    decoded = true;
    std::shared_ptr<MvtTile> tile;
    if (_source.getTile(z, x, y, _scratch)) {
        tile = std::make_shared<MvtTile>();
        if (!decodeMvt(_scratch.data(), _scratch.size(), *tile) && tile->layers.empty())
            tile.reset();
    }
    _lru.push_front(key);
    _cache[key] = { tile, _lru.begin() };
    while (_cache.size() > _limit && _lru.size() > 1) {
        _cache.erase(_lru.back());
        _lru.pop_back();
    }
    return tile;
}

void MapRenderer::render(Canvas& canvas, const Camera& cam, const RenderOptions& opt)
{
    _stats = RenderStats();
    canvas.beginFrame(cam.viewW, cam.viewH, _style.background());

    int z = tileLevel(cam.zoom);
    _stats.tileZoom = z;
    const double n = std::exp2((double)z);

    /* tile range covering the (rotated) viewport */
    const float pad = 24;
    Vec2 corners[4] = { { -pad, -pad }, { cam.viewW + pad, -pad }, { cam.viewW + pad, cam.viewH + pad },
        { -pad, cam.viewH + pad } };
    double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
    for (const Vec2& c : corners) {
        WorldPos w = cam.toWorld(c);
        minX = std::min(minX, w.x);
        minY = std::min(minY, w.y);
        maxX = std::max(maxX, w.x);
        maxY = std::max(maxY, w.y);
    }
    int tx0 = std::max(0, (int)std::floor(minX * n)), tx1 = std::min((int)n - 1, (int)std::floor(maxX * n));
    int ty0 = std::max(0, (int)std::floor(minY * n)), ty1 = std::min((int)n - 1, (int)std::floor(maxY * n));

    const double S = cam.worldToPixel();
    const Matrix base = Matrix::translate(cam.anchor.x, cam.anchor.y) * Matrix::rotate(-cam.bearing);
    const Rect view { -pad, -pad, cam.viewW + pad, cam.viewH + pad };

    std::vector<FrameTile> tiles;
    int decodedNow = 0;
    int budget = opt.loadBudget;
    for (int ty = ty0; ty <= ty1 && tiles.size() < 96; ty++) {
        for (int tx = tx0; tx <= tx1 && tiles.size() < 96; tx++) {
            Matrix m = base * Matrix::translate((float)((tx / n - cam.center.x) * S), (float)((ty / n - cam.center.y) * S))
                * Matrix::scale((float)(S / n), (float)(S / n));
            if (!transformedBounds(m, 0, 0, 1, 1).intersects(view))
                continue;
            bool cached = _cache.count(tileKey(z, tx, ty)) != 0;
            if (!cached && budget > 0 && decodedNow >= budget) {
                _stats.tilesPending++;
                continue;
            }
            bool decoded = false;
            auto tile = loadTile(z, tx, ty, decoded);
            decodedNow += decoded;
            if (!tile) {
                _stats.tilesMissing++;
                continue;
            }
            _stats.tilesDrawn++;
            tiles.push_back({ tx, ty, tile, m });
        }
    }

    for (const StyleLayer& layer : _style.layers())
        drawLayer(canvas, layer, tiles, cam, opt);
    if (opt.labels)
        drawLabels(canvas, tiles, cam, opt);
    canvas.endFrame();
}

void MapRenderer::drawLayer(Canvas& canvas, const StyleLayer& layer, const std::vector<FrameTile>& tiles,
    const Camera& cam, const RenderOptions& opt)
{
    const float zoom = cam.zoom;
    if (zoom < layer.minZoom || zoom >= layer.maxZoom)
        return;
    Color color = layer.color.value;
    color.a = (uint8_t)std::max(0.0f, std::min(255.0f, color.a * layer.opacity.at(zoom)));
    if (color.a == 0)
        return;
    const bool line = layer.kind == StyleLayer::LINE;
    const float width = line ? layer.width.at(zoom) : 0;
    if (line && width < 0.2f)
        return;

    const float tolSq = opt.simplify * opt.simplify;
    const float cull = width + 2;
    const Rect view { -cull, -cull, cam.viewW + cull, cam.viewH + cull };
    _batch.clear();
    for (const FrameTile& ft : tiles) {
        const MvtLayer* ml = ft.tile->find(layer.source);
        if (!ml)
            continue;
        FilterMatcher match(*ml, layer.filter);
        if (match.impossible)
            continue;
        const float inv = 1.0f / (float)ml->extent;
        const Matrix m = ft.toScreen * Matrix::scale(inv, inv);
        for (const MvtFeature& f : ml->features) {
            if (line ? f.type == MvtFeature::POINT : f.type != MvtFeature::POLYGON)
                continue;
            if (f.type == MvtFeature::UNKNOWN)
                continue;
            if (!transformedBounds(m, f.minX, f.minY, f.maxX, f.maxY).intersects(view))
                continue;
            if (!match(*ml, f))
                continue;
            _stats.features++;
            const bool ring = f.type == MvtFeature::POLYGON;
            for (uint32_t p = f.part0; p < f.part0 + f.partN; p++)
                emitPart(_batch, *ml, p, m, tolSq, ring, line ? 0.0f : 1.0f);
        }
    }
    if (_batch.empty())
        return;
    _stats.drawCalls++;
    _stats.vertices += (int)_batch.pts.size();
    if (line) {
        LineStyle ls;
        ls.color = color;
        ls.width = width;
        ls.cap = layer.cap;
        ls.join = layer.join;
        ls.dash = layer.dash.empty() ? nullptr : layer.dash.data();
        ls.dashCount = (int)layer.dash.size();
        canvas.stroke(_batch, ls);
    } else {
        canvas.fill(_batch, color);
    }
}

void MapRenderer::drawLabels(Canvas& canvas, const std::vector<FrameTile>& tiles, const Camera& cam,
    const RenderOptions& opt)
{
    struct Placed {
        Vec2 pos;
        std::string text;
        TextStyle style;
    };
    std::vector<Placed> placed;
    std::vector<Rect> boxes = opt.labelKeepOut;
    std::map<std::string, Vec2> measured;
    const Rect screen { 2, 2, cam.viewW - 2, cam.viewH - 2 };
    const float zoom = cam.zoom;

    for (const StyleLabel& sl : _style.labels()) {
        if (zoom < sl.minZoom || zoom >= sl.maxZoom)
            continue;
        TextStyle ts;
        ts.color = sl.color.value;
        ts.halo = sl.halo.value;
        ts.size = sl.size.at(zoom);
        ts.weight = sl.weight;
        if (ts.size < 4)
            continue;
        const float repeat = std::max(40.0f, sl.repeat);

        auto tryPlace = [&](const std::string& text, Vec2 p) {
            if ((int)placed.size() >= opt.maxLabels)
                return;
            if (p.x < screen.x0 || p.x > screen.x1 || p.y < screen.y0 || p.y > screen.y1)
                return;
            char key[16];
            std::snprintf(key, sizeof key, "|%g|%d", ts.size, ts.weight);
            auto mi = measured.find(text + key);
            if (mi == measured.end())
                mi = measured.insert(std::make_pair(text + key, canvas.measureText(text.c_str(), ts))).first;
            Vec2 sz = mi->second;
            Rect box { p.x - sz.x / 2 - 2, p.y - sz.y / 2 - 1, p.x + sz.x / 2 + 2, p.y + sz.y / 2 + 1 };
            if (box.x0 < 0 || box.y0 < 0 || box.x1 > cam.viewW || box.y1 > cam.viewH)
                return;
            for (const Rect& b : boxes)
                if (box.intersects(b))
                    return;
            for (const Placed& o : placed) {
                if (o.text == text) {
                    float dx = o.pos.x - p.x, dy = o.pos.y - p.y;
                    if (dx * dx + dy * dy < repeat * repeat * 0.5f)
                        return;
                }
            }
            boxes.push_back(box);
            placed.push_back({ p, text, ts });
        };

        for (const FrameTile& ft : tiles) {
            const MvtLayer* ml = ft.tile->find(sl.source);
            if (!ml)
                continue;
            int field = ml->keyIndex(sl.field.c_str());
            if (field < 0)
                continue;
            FilterMatcher match(*ml, sl.filter);
            if (match.impossible)
                continue;
            const float inv = 1.0f / (float)ml->extent;
            const Matrix m = ft.toScreen * Matrix::scale(inv, inv);
            for (const MvtFeature& f : ml->features) {
                const std::string& text = ml->text(f, field);
                if (text.empty() || !match(*ml, f))
                    continue;
                if (!transformedBounds(m, f.minX, f.minY, f.maxX, f.maxY).intersects(screen))
                    continue;
                if (f.type == MvtFeature::POINT) {
                    uint32_t a = ml->partBegin(f.part0);
                    tryPlace(text, m.apply({ (float)ml->points[a].x, (float)ml->points[a].y }));
                } else if (f.type == MvtFeature::POLYGON) {
                    tryPlace(text, m.apply({ (f.minX + f.maxX) * 0.5f, (f.minY + f.maxY) * 0.5f }));
                } else if (f.type == MvtFeature::LINE) {
                    for (uint32_t p = f.part0; p < f.part0 + f.partN; p++) {
                        uint32_t a = ml->partBegin(p), b = ml->partEnd(p);
                        if (b - a < 2)
                            continue;
                        std::vector<Vec2> line;
                        line.reserve(b - a);
                        float total = 0;
                        for (uint32_t i = a; i < b; i++) {
                            line.push_back(m.apply({ (float)ml->points[i].x, (float)ml->points[i].y }));
                            if (line.size() > 1) {
                                float dx = line.back().x - line[line.size() - 2].x;
                                float dy = line.back().y - line[line.size() - 2].y;
                                total += std::sqrt(dx * dx + dy * dy);
                            }
                        }
                        if (total < 36)
                            continue;
                        /* anchors every `repeat` px, centred on the line */
                        float s = total * 0.5f;
                        s -= std::floor(s / repeat) * repeat;
                        for (int guard = 0; s < total && guard < 12; s += repeat, guard++) {
                            float acc = 0;
                            for (size_t i = 1; i < line.size(); i++) {
                                float dx = line[i].x - line[i - 1].x, dy = line[i].y - line[i - 1].y;
                                float seg = std::sqrt(dx * dx + dy * dy);
                                if (acc + seg >= s && seg > 0) {
                                    float t = (s - acc) / seg;
                                    tryPlace(text, { line[i - 1].x + dx * t, line[i - 1].y + dy * t });
                                    break;
                                }
                                acc += seg;
                            }
                        }
                    }
                }
            }
        }
    }
    for (const Placed& p : placed)
        canvas.drawText(p.pos, p.text.c_str(), p.style);
    _stats.labelsPlaced = (int)placed.size();
}

} // namespace MapTile
