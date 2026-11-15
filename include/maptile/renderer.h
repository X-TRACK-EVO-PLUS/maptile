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
#include "maptile/tile_store.h"
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
    /* Do not read and decode a tile that is not in the store: it is left out and counted in RenderStats::tilesPending.
     * For a frame that must not wait (another thread is decoding what it needs). */
    bool onlyCached = false;
};

/* A tile of the source */
struct TileId {
    int z, x, y;
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
    /* A renderer with the decoded tiles of its own */
    MapRenderer(TileSource& source, const Style& style);
    /* A renderer that shares the decoded tiles of `store` with others, on other threads too. Each thread has a renderer of
     * its own: one renderer is not for two threads. */
    MapRenderer(TileStore& store, const Style& style);
    void setTileCacheLimit(size_t tiles) { _store->setLimit(tiles); }
    void clearCache() { _store->clear(); }
    /* The tile level used for a zoom: nearest integer, kept inside the source's range */
    int tileLevel(float zoom) const;

    void render(Canvas& canvas, const Camera& camera, const RenderOptions& options = RenderOptions());
    /* Only the labels of the tiles the camera sees, over what the canvas has already: for a map whose tiles are
     * drawn as bitmaps (drawBitmapTile) and kept, labels being the part that has to be laid out again for every
     * frame, since where they go depends on what else is in the view */
    void renderLabels(Canvas& canvas, const Camera& camera, const RenderOptions& options = RenderOptions());
    /* One tile of the grid of `level` (not above 22; the source's last level is drawn larger) as a bitmap, without
     * labels: `zoom` is the zoom it is drawn for, which sets its size, bitmapPixels(level, zoom) pixels across the tile,
     * and `bleed` pixels more are drawn on each side, so that the bitmaps of neighbours overlap. The canvas is
     * bitmapPixels rounded up plus twice the bleed, square. False if `level` is out of range (nothing drawn); a tile
     * the source does not have is the background. */
    bool drawBitmapTile(Canvas& canvas, int level, int x, int y, float zoom, int bleed, const RenderOptions& options = RenderOptions());
    static float bitmapPixels(int level, float zoom);
    /* The tiles of the source that renderLabels() would use for `camera` and that are not decoded yet: for a frame that
     * draws with onlyCached, what to have decoded elsewhere */
    std::vector<TileId> uncachedTiles(const Camera& camera);
    void resetStats() { _stats = RenderStats(); }
    const RenderStats& stats() const { return _stats; }

private:
    struct FrameTile {
        int x, y;
        std::shared_ptr<const MvtTile> tile;
        Matrix toScreen; /* tile units -> screen pixels */
    };

    std::shared_ptr<const MvtTile> loadTile(int z, int x, int y, bool& decoded);
    std::vector<FrameTile> collectTiles(const Camera& camera, const RenderOptions& options);
    void drawLayer(Canvas& canvas, const StyleLayer& layer, const std::vector<FrameTile>& tiles, const Camera& cam,
        const RenderOptions& opt);
    void drawLabels(Canvas& canvas, const std::vector<FrameTile>& tiles, const Camera& cam, const RenderOptions& opt);

    std::unique_ptr<TileStore> _own;
    TileStore* _store;
    const Style& _style;
    RenderStats _stats;
    std::vector<TileId> _pending; /* what collectTiles left out, the last time */
    PathBatch _batch;
};

} // namespace MapTile

#endif // MAPTILE_RENDERER_H
