/*
 * MIT License
 * Copyright (c) 2024 HanfG, _VIFEXTech
 */
#ifndef MAPTILE_MBTILES_READER_H
#define MAPTILE_MBTILES_READER_H

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace MapTile {

class MBTilesReader {
public:
    MBTilesReader();
    ~MBTilesReader();

    bool open(const char* path);
    void close();
    bool isOpen() const { return _db != nullptr; }

    bool getTileData(int zoom, int x, int y, std::vector<uint8_t>& data);

    std::string getMetadata(const std::string& key);
    int getMinZoom();
    int getMaxZoom();

private:
    static bool gunzip(const void* src, int srcLen, std::vector<uint8_t>& dst);
    sqlite3* _db;
};

} // namespace MapTile

#endif // MAPTILE_MBTILES_READER_H
