/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_TILE_STORE_H
#define MAPTILE_TILE_STORE_H
#include "maptile/mvt.h"
#include "maptile/tile_source.h"
#include <condition_variable>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
namespace MapTile {
/*
 * The decoded tiles of a source that were used last: what a renderer asks for, kept so that the next frame does not read and
 * decode the tile again. Any number of threads may ask at once; a tile that two of them want is read and decoded by one while
 * the other waits for it. The tiles it hands out never change, so they can be drawn from on any thread.
 */
class TileStore {
public:
    explicit TileStore(TileSource& source, size_t limit = 48);
    /* The tile, or null if the source has none (or it is damaged). `decoded` tells whether it was read from the source now. */
    std::shared_ptr<const MvtTile> get(int z, int x, int y, bool* decoded = nullptr);
    /* Whether get() would answer at once */
    bool cached(int z, int x, int y) const;
    void setLimit(size_t tiles);
    void clear();
    size_t size() const;
    int minZoom() const { return _source.minZoom(); }
    int maxZoom() const { return _source.maxZoom(); }

private:
    struct Entry {
        std::shared_ptr<const MvtTile> tile; /* null: not available */
        std::list<uint64_t>::iterator lru;
    };
    TileSource& _source;
    mutable std::mutex _mutex; /* the cache, its order, the tiles being read */
    std::condition_variable _loaded;
    std::map<uint64_t, Entry> _cache;
    std::list<uint64_t> _lru;
    std::set<uint64_t> _loading;
    size_t _limit;
    std::mutex _sourceMutex; /* a source is read by one thread at a time */
};
} // namespace MapTile
#endif
