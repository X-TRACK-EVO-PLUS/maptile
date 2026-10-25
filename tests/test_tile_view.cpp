#include "maptile/tile_view.h"
#include "test_common.h"

TEST_CASE("TileView::constructor defaults")
{
    TileView tv(800, 600, 256);
    CHECK(tv.getViewWidth() == 800);
    CHECK(tv.getViewHeight() == 600);
    CHECK(tv.getTileSize() == 256);
}

TEST_CASE("TileView::setFocusPos and getFocusPos")
{
    TileView tv(800, 600, 256);
    tv.setFocusPos(1000, 2000);

    TileView::Point_t pos = tv.getFocusPos();
    CHECK(pos.x == 1000);
    CHECK(pos.y == 2000);
}

TEST_CASE("TileView::getTileNum returns reasonable count")
{
    TileView tv(800, 600, 256);
    tv.setFocusPos(1000, 1000);

    uint32_t num = tv.getTileNum();
    // 800/256 = ~3.1 → 4 cols, 600/256 = ~2.3 → 3 rows, +1 each for overlap
    // Expect somewhere between 12 and 25 tiles
    CHECK(num >= 9);
    CHECK(num <= 30);
}

TEST_CASE("TileView::getTileRect")
{
    TileView tv(800, 600, 256);
    tv.setFocusPos(5000, 5000);

    TileView::Rect_t rect = tv.getTileRect();
    CHECK(rect.width > 0);
    CHECK(rect.height > 0);
    // Rect should be at least as large as the view
    CHECK(rect.width >= 800);
    CHECK(rect.height >= 600);
}

TEST_CASE("TileView::getTilePos within bounds")
{
    TileView tv(800, 600, 256);
    tv.setFocusPos(5000, 5000);

    uint32_t num = tv.getTileNum();
    for (uint32_t i = 0; i < num; i++) {
        TileView::Point_t pos = tv.getTilePos(i);
        // Tile positions should be multiples of tile size
        CHECK(pos.x % 256 == 0);
        CHECK(pos.y % 256 == 0);
    }
}

TEST_CASE("TileView::setViewSize")
{
    TileView tv(400, 300, 256);
    CHECK(tv.getViewWidth() == 400);

    tv.setViewSize(1024, 768);
    CHECK(tv.getViewWidth() == 1024);
    CHECK(tv.getViewHeight() == 768);
}
