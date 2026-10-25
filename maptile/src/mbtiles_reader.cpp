/*
 * MIT License
 * Copyright (c) 2024 HanfG, _VIFEXTech
 */
#include "maptile/mbtiles_reader.h"
#include <sqlite3.h>
#include <zlib.h>

using namespace MapTile;

MBTilesReader::MBTilesReader()
    : _db(nullptr)
{
}

MBTilesReader::~MBTilesReader()
{
    close();
}

bool MBTilesReader::open(const char* path)
{
    close();
    int rc = sqlite3_open_v2(path, &_db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        _db = nullptr;
        return false;
    }
    return true;
}

void MBTilesReader::close()
{
    if (_db) {
        sqlite3_close(_db);
        _db = nullptr;
    }
}

bool MBTilesReader::getTileData(int zoom, int x, int y, std::vector<uint8_t>& data)
{
    if (!_db)
        return false;

    /* MBTiles uses TMS y-flip */
    int tmsY = (1 << zoom) - 1 - y;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?";
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt, 1, zoom);
    sqlite3_bind_int(stmt, 2, x);
    sqlite3_bind_int(stmt, 3, tmsY);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int blobLen = sqlite3_column_bytes(stmt, 0);
        if (blob && blobLen > 0) {
            const uint8_t* bytes = (const uint8_t*)blob;
            /* Check if gzip compressed */
            if (blobLen >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b) {
                found = gunzip(blob, blobLen, data);
            } else {
                data.assign(bytes, bytes + blobLen);
                found = true;
            }
        }
    }

    sqlite3_finalize(stmt);
    return found;
}

std::string MBTilesReader::getMetadata(const std::string& key)
{
    if (!_db)
        return "";

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT value FROM metadata WHERE name=?";
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return "";

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);

    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* val = (const char*)sqlite3_column_text(stmt, 0);
        if (val)
            result = val;
    }

    sqlite3_finalize(stmt);
    return result;
}

int MBTilesReader::getMinZoom()
{
    /* Query actual tiles table for reliable result */
    if (_db) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(_db, "SELECT MIN(zoom_level) FROM tiles", -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
                int val = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
                return val;
            }
            sqlite3_finalize(stmt);
        }
    }
    std::string val = getMetadata("minzoom");
    return val.empty() ? 0 : std::stoi(val);
}

int MBTilesReader::getMaxZoom()
{
    /* Query actual tiles table for reliable result */
    if (_db) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(_db, "SELECT MAX(zoom_level) FROM tiles", -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
                int val = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
                return val;
            }
            sqlite3_finalize(stmt);
        }
    }
    std::string val = getMetadata("maxzoom");
    return val.empty() ? 14 : std::stoi(val);
}

bool MBTilesReader::gunzip(const void* src, int srcLen, std::vector<uint8_t>& dst)
{
    z_stream strm = {};
    strm.next_in = (Bytef*)src;
    strm.avail_in = srcLen;

    if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK)
        return false;

    dst.resize(srcLen * 4); /* initial guess */
    strm.next_out = dst.data();
    strm.avail_out = (uInt)dst.size();

    int ret;
    while ((ret = inflate(&strm, Z_NO_FLUSH)) != Z_STREAM_END) {
        if (ret != Z_OK && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return false;
        }
        if (strm.avail_out == 0) {
            size_t oldSize = dst.size();
            dst.resize(oldSize * 2);
            strm.next_out = dst.data() + oldSize;
            strm.avail_out = (uInt)(dst.size() - oldSize);
        }
    }

    dst.resize(strm.total_out);
    inflateEnd(&strm);
    return true;
}
