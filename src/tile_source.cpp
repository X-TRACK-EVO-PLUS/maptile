/*
 * MIT License
 * Copyright (c) 2024 - 2026 HanfG, _VIFEXTech
 */
#include "maptile/tile_source.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef MAPTILE_WITH_ZLIB
#include <zlib.h>
#endif
#ifdef MAPTILE_WITH_SQLITE
#include <sqlite3.h>
#endif

namespace MapTile {

bool gunzipMaybe(const uint8_t* src, size_t size, std::vector<uint8_t>& dst)
{
    dst.clear();
    bool gz = size > 2 && src[0] == 0x1f && src[1] == 0x8b;
    if (!gz) {
        dst.assign(src, src + size);
        return true;
    }
#ifdef MAPTILE_WITH_ZLIB
    z_stream zs;
    std::memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)
        return false;
    zs.next_in = (Bytef*)src;
    zs.avail_in = (uInt)size;
    uint8_t buf[16384];
    int rc;
    do {
        zs.next_out = buf;
        zs.avail_out = sizeof buf;
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&zs);
            dst.clear();
            return false;
        }
        dst.insert(dst.end(), buf, buf + (sizeof buf - zs.avail_out));
    } while (rc != Z_STREAM_END);
    inflateEnd(&zs);
    return true;
#else
    return false;
#endif
}

DirectorySource::DirectorySource(const std::string& pattern, int minZ, int maxZ)
    : _pattern(pattern)
    , _minZoom(minZ)
    , _maxZoom(maxZ)
{
}

bool DirectorySource::getTile(int z, int x, int y, std::vector<uint8_t>& out)
{
    char path[512];
    std::snprintf(path, sizeof path, _pattern.c_str(), z, x, y);
    FILE* f = std::fopen(path, "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> raw(n > 0 ? (size_t)n : 0);
    bool ok = n > 0 && std::fread(raw.data(), 1, raw.size(), f) == raw.size();
    std::fclose(f);
    return ok && gunzipMaybe(raw.data(), raw.size(), out);
}

#ifdef MAPTILE_WITH_SQLITE

MBTilesSource::MBTilesSource() = default;
MBTilesSource::~MBTilesSource() { close(); }
bool MBTilesSource::available() { return true; }

bool MBTilesSource::open(const char* path)
{
    close();
    if (sqlite3_open_v2(path, &_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        close();
        return false;
    }
    std::string lo = metadata("minzoom"), hi = metadata("maxzoom");
    if (!lo.empty())
        _minZoom = std::atoi(lo.c_str());
    if (!hi.empty())
        _maxZoom = std::atoi(hi.c_str());
    return true;
}

void MBTilesSource::close()
{
    if (_db)
        sqlite3_close(_db);
    _db = nullptr;
}

std::string MBTilesSource::metadata(const std::string& key)
{
    std::string value;
    sqlite3_stmt* st = nullptr;
    if (_db && sqlite3_prepare_v2(_db, "SELECT value FROM metadata WHERE name=?", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* t = sqlite3_column_text(st, 0);
            if (t)
                value = (const char*)t;
        }
    }
    if (st)
        sqlite3_finalize(st);
    return value;
}

bool MBTilesSource::getTile(int z, int x, int y, std::vector<uint8_t>& out)
{
    if (!_db)
        return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(_db, "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?", -1, &st,
            nullptr)
        != SQLITE_OK)
        return false;
    sqlite3_bind_int(st, 1, z);
    sqlite3_bind_int(st, 2, x);
    sqlite3_bind_int(st, 3, (1 << z) - 1 - y); /* MBTiles rows are TMS */
    bool ok = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(st, 0);
        int n = sqlite3_column_bytes(st, 0);
        ok = blob && n > 0 && gunzipMaybe((const uint8_t*)blob, (size_t)n, out);
    }
    sqlite3_finalize(st);
    return ok;
}

#else

MBTilesSource::MBTilesSource() = default;
MBTilesSource::~MBTilesSource() = default;
bool MBTilesSource::available() { return false; }
bool MBTilesSource::open(const char*) { return false; }
void MBTilesSource::close() { }
bool MBTilesSource::getTile(int, int, int, std::vector<uint8_t>&) { return false; }
std::string MBTilesSource::metadata(const std::string&) { return std::string(); }

#endif

} // namespace MapTile
