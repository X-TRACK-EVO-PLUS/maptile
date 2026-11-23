/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile/tile_store.h"
namespace MapTile {
namespace {
    uint64_t tileKey(int z, int x, int y) { return (uint64_t(z) << 52) | (uint64_t(x) << 26) | uint64_t(y); }
}
TileStore::TileStore(TileSource& source, size_t limit)
    : _source(source)
    , _limit(limit)
{
}
bool TileStore::cached(int z, int x, int y) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _cache.count(tileKey(z, x, y)) != 0;
}
bool TileStore::find(int z, int x, int y, std::shared_ptr<const MvtTile>& out)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _cache.find(tileKey(z, x, y));
    if (it == _cache.end()) {
        return false;
    }
    _lru.splice(_lru.begin(), _lru, it->second.lru);
    out = it->second.tile;
    return true;
}
void TileStore::setLimit(size_t tiles)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _limit = tiles;
    while (_cache.size() > _limit && _lru.size() > 1) {
        _cache.erase(_lru.back());
        _lru.pop_back();
    }
}
void TileStore::clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _cache.clear();
    _lru.clear();
}
size_t TileStore::size() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _cache.size();
}
std::shared_ptr<const MvtTile> TileStore::get(int z, int x, int y, bool* decoded)
{
    const uint64_t key = tileKey(z, x, y);
    if (decoded) {
        *decoded = false;
    }
    {
        std::unique_lock<std::mutex> lock(_mutex);
        for (;;) {
            auto it = _cache.find(key);
            if (it != _cache.end()) {
                _lru.splice(_lru.begin(), _lru, it->second.lru);
                return it->second.tile;
            }
            if (!_loading.count(key)) {
                break;
            }
            /* another thread is on it */
            _loaded.wait(lock);
        }
        _loading.insert(key);
    }
    std::shared_ptr<MvtTile> tile;
    try {
        std::vector<uint8_t> bytes;
        bool got;
        {
            std::lock_guard<std::mutex> reading(_sourceMutex);
            got = _source.getTile(z, x, y, bytes);
        }
        if (got) {
            tile = std::make_shared<MvtTile>();
            if (!decodeMvt(bytes.data(), bytes.size(), *tile) && tile->layers.empty()) {
                tile.reset();
            }
        }
    } catch (...) {
        /* The source or the decoder threw (out of memory, a damaged file). The key must not stay in _loading: the threads that wait
         * for this tile would wait for ever. Nothing is kept for it, so that a later frame asks again; this one has no tile. */
        std::lock_guard<std::mutex> lock(_mutex);
        _loading.erase(key);
        _loaded.notify_all();
        return nullptr;
    }
    if (decoded) {
        *decoded = true;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    _lru.push_front(key);
    _cache[key] = { tile, _lru.begin() };
    _loading.erase(key);
    while (_cache.size() > _limit && _lru.size() > 1) {
        _cache.erase(_lru.back());
        _lru.pop_back();
    }
    _loaded.notify_all();
    return tile;
}
} // namespace MapTile
