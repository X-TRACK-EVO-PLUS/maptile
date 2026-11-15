/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_BITMAP_POOL_H
#define MAPTILE_BITMAP_POOL_H
#include "maptile/renderer.h"
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
namespace MapTile {
/* A tile drawn into pixels (0xFFRRGGBB, `size` square, 64-byte aligned) */
struct TileBitmap {
    int level = 0, x = 0, y = 0;
    float zoom = 0; /* the zoom it was drawn for */
    float pixels = 0; /* the tile's width in it: bitmapPixels(level, zoom) */
    int bleed = 0;
    int size = 0;
    uint32_t generation = 0;
    std::unique_ptr<uint32_t, void (*)(void*)> data { nullptr, std::free };
};
/* Draws the tile asked for in `out` (level, x, y, zoom, bleed) on this thread; false if it is not a tile there can be */
bool drawTileBitmap(MapRenderer& renderer, TileBitmap& out);
/*
 * Threads that draw tiles into bitmaps, so that the thread of the screen does not wait for any. What is wanted is a list in
 * the order of need; asking again replaces what has not been started. The renderers of the threads share one TileStore,
 * the style is a copy that is not changed while they read it.
 *
 * With no threads (tests) want() draws everything on the spot. `nice` above 0 runs the threads that much lower than the
 * one that made the pool (Linux; elsewhere it is ignored), so that a screen they draw for does not wait for them.
 */
class BitmapPool {
public:
    /* A tile to draw; or, `decodeOnly`, the tile of the source at level, x, y to have read and decoded in the store (for the
     * labels of a frame that does not wait), nothing to take */
    struct Job {
        int level, x, y;
        float zoom;
        int bleed;
        bool decodeOnly = false;
        bool operator==(const Job& o) const
        {
            return level == o.level && x == o.x && y == o.y && zoom == o.zoom && bleed == o.bleed && decodeOnly == o.decodeOnly;
        }
    };
    BitmapPool(TileStore& store, int threads, int nice = 0);
    ~BitmapPool();
    BitmapPool(const BitmapPool&) = delete;
    BitmapPool& operator=(const BitmapPool&) = delete;
    /* The style the tiles are drawn with from now on (a copy is kept); what was queued is dropped, and what is being drawn
     * or done is thrown away when it comes out */
    void setStyle(const Style& style);
    /* The tiles wanted now, the most wanted first */
    void want(const std::vector<Job>& jobs);
    /* The bitmaps that are done, oldest first */
    std::vector<TileBitmap> take();
    /* Something is queued or being drawn */
    bool busy() const;
    int threads() const { return (int)_threads.size(); }

private:
    void work();
    void run(const Job& job, const std::shared_ptr<const Style>& style, uint32_t generation);
    TileStore& _store;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    std::deque<Job> _queue;
    std::vector<Job> _running;
    std::vector<TileBitmap> _done;
    std::shared_ptr<const Style> _style;
    uint32_t _generation = 1;
    bool _stop = false;
    int _nice = 0;
    std::vector<std::thread> _threads;
};
} // namespace MapTile
#endif
