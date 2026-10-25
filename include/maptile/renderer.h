/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_RENDERER_H
#define MAPTILE_RENDERER_H
#include "maptile/camera.h"
#include "maptile/canvas.h"
#include "maptile/mvt.h"
#include "maptile/style.h"
#include "maptile/tile_source.h"
#include <list>
#include <map>
#include <memory>

namespace MapTile {

struct RenderOptions {
    /* Screen rectangles labels must stay clear of (UI panels, the rider marker) */
    std::vector<Rect> labelKeepOut;
    int maxLabels = 48;
    /* Screen-space simplification of geometry, pixels */
    float simplify = 0.6f;
    /* Tiles to decode per frame; 0 = no limit. Tiles over the budget are left
     * out and counted in RenderStats::tilesPending, so the caller can draw
     * again on the next frame instead of stalling this one. */
    int loadBudget = 0;
    bool labels = true;
};

struct RenderStats {
    int tileZoom = 0;
    int tilesDrawn = 0;
    int tilesMissing = 0; /* not in the source */
    int tilesPending = 0; /* over the load budget */
    int features = 0;
    int vertices = 0;
    int drawCalls = 0;
    int labelsPlaced = 0;
};

/*
 * Draws the map seen by a Camera onto a Canvas: every visible tile of the
 * source, through the style, in one pass over the whole viewport. Zoom and
 * bearing are continuous; geometry is projected per frame, so nothing is
 * rasterised until the canvas does it.
 */
class MapRenderer {
public:
    MapRenderer(TileSource& source, Style& style);

    void setTileCacheLimit(size_t tiles) { _limit = tiles; }
    void clearCache();
    /* The tile level used for a zoom: nearest integer, kept inside the source's range */
    int tileLevel(float zoom) const;

    void render(Canvas& canvas, const Camera& camera, const RenderOptions& options = RenderOptions());
    const RenderStats& stats() const { return _stats; }

private:
    struct FrameTile {
        int x, y;
        std::shared_ptr<const MvtTile> tile;
        Matrix toScreen; /* tile units -> screen pixels */
    };
    struct CacheEntry {
        std::shared_ptr<const MvtTile> tile; /* null: not available */
        std::list<uint64_t>::iterator lru;
    };

    std::shared_ptr<const MvtTile> loadTile(int z, int x, int y, bool& decoded);
    void drawLayer(Canvas& canvas, const StyleLayer& layer, const std::vector<FrameTile>& tiles, const Camera& cam,
        const RenderOptions& opt);
    void drawLabels(Canvas& canvas, const std::vector<FrameTile>& tiles, const Camera& cam, const RenderOptions& opt);

    TileSource& _source;
    Style& _style;
    size_t _limit = 48;
    std::map<uint64_t, CacheEntry> _cache;
    std::list<uint64_t> _lru;
    std::vector<uint8_t> _scratch;
    RenderStats _stats;
    PathBatch _batch;
};

} // namespace MapTile

#endif // MAPTILE_RENDERER_H
