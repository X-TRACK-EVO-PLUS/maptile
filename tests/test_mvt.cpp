#include "maptile/mvt.h"
#include "maptile/tile_source.h"
#include "test_common.h"
#include <cstring>

using namespace MapTile;

static const char* MINI = "tests/data/beijing-mini.mbtiles";

TEST_CASE("decodeMvt: a hand-built tile")
{
    /* layer "t" (extent 4096), key "kind" = "park"; a square polygon
     * (10,10) (20,10) (10,20)... as MoveTo/LineTo/ClosePath, and a point at (1,2) */
    const uint8_t bytes[] = { 0x1a, 0x30, 0x0a, 0x01, 0x74, 0x12, 0x11, 0x12, 0x02, 0x00, 0x00, 0x18, 0x03, 0x22, 0x09,
        0x09, 0x14, 0x14, 0x12, 0x14, 0x00, 0x13, 0x14, 0x0f, 0x12, 0x07, 0x18, 0x01, 0x22, 0x03, 0x09, 0x02, 0x04,
        0x1a, 0x04, 0x6b, 0x69, 0x6e, 0x64, 0x22, 0x06, 0x0a, 0x04, 0x70, 0x61, 0x72, 0x6b, 0x28, 0x80, 0x20 };
    MvtTile t;
    REQUIRE(decodeMvt(bytes, sizeof bytes, t));
    REQUIRE(t.layers.size() == 1);
    const MvtLayer& l = t.layers[0];
    CHECK(l.name == "t");
    CHECK(l.extent == 4096);
    REQUIRE(l.features.size() == 2);

    const MvtFeature& poly = l.features[0];
    CHECK(poly.type == MvtFeature::POLYGON);
    REQUIRE(poly.partN == 1);
    REQUIRE(l.partEnd(poly.part0) - l.partBegin(poly.part0) == 3);
    CHECK(l.points[l.partBegin(poly.part0)].x == 10);
    CHECK(l.points[l.partBegin(poly.part0) + 1].x == 20);
    CHECK(l.points[l.partBegin(poly.part0) + 2].x == 10);
    CHECK(l.points[l.partBegin(poly.part0) + 2].y == 20);
    CHECK(poly.minX == 10);
    CHECK(poly.maxX == 20);
    CHECK(poly.maxY == 20);
    CHECK(l.text(poly, l.keyIndex("kind")) == "park");
    CHECK(l.keyIndex("nope") == -1);

    const MvtFeature& pt = l.features[1];
    CHECK(pt.type == MvtFeature::POINT);
    CHECK(l.points[l.partBegin(pt.part0)].x == 1);
    CHECK(l.points[l.partBegin(pt.part0)].y == 2);
    CHECK(l.text(pt, l.keyIndex("kind")).empty());

    /* malformed input is rejected, not crashed on */
    MvtTile junk;
    const uint8_t bad[] = { 0x1a, 0xff, 0xff };
    CHECK_FALSE(decodeMvt(bad, sizeof bad, junk));
    for (size_t cut = 1; cut < sizeof bytes; cut++) {
        MvtTile partial;
        decodeMvt(bytes, cut, partial); /* any prefix must be safe */
    }
}

TEST_CASE("decodeMvt: a real tile from the fixture")
{
    MBTilesSource src;
    if (!MBTilesSource::available()) {
        MESSAGE("built without sqlite3, skipping");
        return;
    }
    REQUIRE(src.open(MINI));
    CHECK(src.minZoom() == 13);
    CHECK(src.maxZoom() == 14);

    std::vector<uint8_t> bytes;
    REQUIRE(src.getTile(14, 13489, 6207, bytes));
    MvtTile tile;
    REQUIRE(decodeMvt(bytes.data(), bytes.size(), tile));

    const MvtLayer* streets = tile.find("streets");
    REQUIRE(streets);
    CHECK(streets->extent == 4096);
    CHECK(streets->features.size() > 100);
    int kind = streets->keyIndex("kind");
    REQUIRE(kind >= 0);

    size_t lines = 0, withKind = 0;
    for (const MvtFeature& f : streets->features) {
        CHECK(f.partN >= 1);
        CHECK(f.minX <= f.maxX);
        CHECK(f.minY <= f.maxY);
        if (f.type == MvtFeature::LINE)
            lines++;
        if (!streets->text(f, kind).empty())
            withKind++;
        for (uint32_t p = f.part0; p < f.part0 + f.partN; p++) {
            CHECK(streets->partEnd(p) > streets->partBegin(p));
            CHECK(streets->partEnd(p) <= streets->points.size());
        }
    }
    CHECK(lines == streets->features.size());
    CHECK(withKind == streets->features.size());

    /* the city centre has a lot of water and buildings */
    CHECK(tile.find("buildings"));
    const MvtLayer* water = tile.find("water_polygons");
    REQUIRE(water);
    for (const MvtFeature& f : water->features)
        CHECK(f.type == MvtFeature::POLYGON);
    /* names are UTF-8 */
    const MvtLayer* labels = tile.find("street_labels");
    REQUIRE(labels);
    int name = labels->keyIndex("name");
    REQUIRE(name >= 0);
    bool cjk = false;
    for (const MvtFeature& f : labels->features) {
        const std::string& s = labels->text(f, name);
        cjk = cjk || (s.size() >= 3 && (unsigned char)s[0] >= 0xe4);
    }
    CHECK(cjk);
}

TEST_CASE("MBTilesSource: missing tiles and bad paths")
{
    MBTilesSource src;
    if (!MBTilesSource::available())
        return;
    CHECK_FALSE(src.open("tests/data/does-not-exist.mbtiles"));
    REQUIRE(src.open(MINI));
    std::vector<uint8_t> bytes;
    CHECK_FALSE(src.getTile(14, 1, 1, bytes));
    CHECK(src.getTile(13, 6744, 3103, bytes));
}

TEST_CASE("gunzipMaybe passes plain data through")
{
    const uint8_t plain[] = { 1, 2, 3, 4 };
    std::vector<uint8_t> out;
    CHECK(gunzipMaybe(plain, sizeof plain, out));
    CHECK(out.size() == 4);
}
