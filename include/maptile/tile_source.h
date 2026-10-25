/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_TILE_SOURCE_H
#define MAPTILE_TILE_SOURCE_H
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct sqlite3;

namespace MapTile {

/* Where vector tiles come from. getTile() returns the raw MVT bytes,
 * already gunzipped. XYZ addressing (y grows southwards). */
class TileSource {
public:
    virtual ~TileSource() = default;
    virtual bool getTile(int z, int x, int y, std::vector<uint8_t>& out) = 0;
    virtual int minZoom() const { return 0; }
    virtual int maxZoom() const { return 14; }
};

/* Inflate a gzip or zlib stream; returns false if built without zlib or on error.
 * Data that is not compressed is copied through unchanged. */
bool gunzipMaybe(const uint8_t* src, size_t size, std::vector<uint8_t>& dst);

/* A directory of z/x/y files; `pattern` is a printf format taking z, x, y,
 * e.g. "/mnt/sdcard/Maps/osm/%d/%d/%d.mvt". */
class DirectorySource : public TileSource {
public:
    DirectorySource(const std::string& pattern, int minZoom = 0, int maxZoom = 14);
    bool getTile(int z, int x, int y, std::vector<uint8_t>& out) override;
    int minZoom() const override { return _minZoom; }
    int maxZoom() const override { return _maxZoom; }

private:
    std::string _pattern;
    int _minZoom, _maxZoom;
};

/* An MBTiles (SQLite) file. Available when built with MAPTILE_WITH_SQLITE. */
class MBTilesSource : public TileSource {
public:
    MBTilesSource();
    ~MBTilesSource() override;
    static bool available();
    bool open(const char* path);
    void close();
    bool isOpen() const { return _db != nullptr; }
    bool getTile(int z, int x, int y, std::vector<uint8_t>& out) override;
    int minZoom() const override { return _minZoom; }
    int maxZoom() const override { return _maxZoom; }
    std::string metadata(const std::string& key);

private:
    sqlite3* _db = nullptr;
    int _minZoom = 0, _maxZoom = 14;
};

} // namespace MapTile

#endif // MAPTILE_TILE_SOURCE_H
