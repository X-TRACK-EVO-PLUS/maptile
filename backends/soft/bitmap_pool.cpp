/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "bitmap_pool.h"
#include "soft_canvas.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#if defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace MapTile {
bool drawTileBitmap(MapRenderer& renderer, TileBitmap& out)
{
    out.pixels = MapRenderer::bitmapPixels(out.level, out.zoom);
    out.size = (int)std::ceil(out.pixels) + 2 * out.bleed;
    if (out.size <= 0 || out.size > SoftCanvas::MAX_WIDTH) {
        return false;
    }
    const size_t bytes = ((size_t)out.size * (size_t)out.size * 4 + 63) & ~(size_t)63;
    void* mem = ::aligned_alloc(64, bytes);
    if (!mem) {
        return false;
    }
    out.data.reset(static_cast<uint32_t*>(mem));
    SoftCanvas canvas(out.data.get(), out.size, out.size, out.size * 4);
    RenderOptions opt;
    opt.labels = false;
    if (!renderer.drawBitmapTile(canvas, out.level, out.x, out.y, out.zoom, out.bleed, opt)) {
        out.data.reset();
        return false;
    }
    return true;
}
BitmapPool::BitmapPool(TileStore& store, int threads, int nice)
    : _store(store)
    , _nice(nice)
{
    for (int i = 0; i < threads; i++) {
        _threads.emplace_back([this] { work(); });
    }
}
BitmapPool::~BitmapPool()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
        _queue.clear();
    }
    _wake.notify_all();
    for (auto& t : _threads) {
        t.join();
    }
}
void BitmapPool::setStyle(const Style& style)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _style = std::make_shared<const Style>(style);
    _generation++;
    _queue.clear();
    _done.clear();
}
void BitmapPool::run(const Job& job, const std::shared_ptr<const Style>& style, uint32_t generation)
{
    if (job.decodeOnly) {
        _store.get(job.level, job.x, job.y);
        std::lock_guard<std::mutex> lock(_mutex);
        _running.erase(std::remove(_running.begin(), _running.end(), job), _running.end());
        return;
    }
    MapRenderer renderer(_store, *style);
    TileBitmap bitmap;
    bitmap.level = job.level;
    bitmap.x = job.x;
    bitmap.y = job.y;
    bitmap.zoom = job.zoom;
    bitmap.bleed = job.bleed;
    bitmap.generation = generation;
    const bool drawn = drawTileBitmap(renderer, bitmap);
    std::lock_guard<std::mutex> lock(_mutex);
    _running.erase(std::remove(_running.begin(), _running.end(), job), _running.end());
    if (drawn && generation == _generation) {
        _done.push_back(std::move(bitmap));
    }
}
void BitmapPool::want(const std::vector<Job>& jobs)
{
    std::shared_ptr<const Style> style;
    uint32_t generation;
    std::vector<Job> now;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_style) {
            return;
        }
        _queue.clear();
        for (const Job& j : jobs) {
            if (std::find(_running.begin(), _running.end(), j) == _running.end()) {
                _queue.push_back(j);
            }
        }
        if (_threads.empty()) {
            now.assign(_queue.begin(), _queue.end());
            _queue.clear();
            style = _style;
            generation = _generation;
        }
    }
    if (_threads.empty()) {
        for (const Job& j : now) {
            run(j, style, generation);
        }
        return;
    }
    _wake.notify_all();
}
std::vector<TileBitmap> BitmapPool::take()
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<TileBitmap> out;
    out.swap(_done);
    return out;
}
bool BitmapPool::busy() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return !_queue.empty() || !_running.empty();
}
void BitmapPool::work()
{
#if defined(__linux__)
    /* the priority of a thread of its own: setpriority() takes the thread's id on Linux */
    if (_nice > 0) {
        setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), _nice);
    }
#endif
    for (;;) {
        Job job;
        std::shared_ptr<const Style> style;
        uint32_t generation;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _wake.wait(lock, [this] { return _stop || !_queue.empty(); });
            if (_stop) {
                return;
            }
            job = _queue.front();
            _queue.pop_front();
            _running.push_back(job);
            style = _style;
            generation = _generation;
        }
        run(job, style, generation);
    }
}
} // namespace MapTile
