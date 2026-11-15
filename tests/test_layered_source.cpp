#include "maptile/tile_source.h"
#include "test_common.h"
#include <map>
using namespace MapTile;
namespace {
/* A source of a few tiles, each holding one byte that says whose it is */
class FakeSource : public TileSource {
public:
    FakeSource(uint8_t who, int minZ, int maxZ)
        : _who(who)
        , _minZ(minZ)
        , _maxZ(maxZ)
    {
    }
    void add(int z, int x, int y) { _has[{ z, x, y }] = true; }
    bool getTile(int z, int x, int y, std::vector<uint8_t>& out) override
    {
        asked++;
        if (!_has.count({ z, x, y }))
            return false;
        out.assign(1, _who);
        return true;
    }
    int minZoom() const override { return _minZ; }
    int maxZoom() const override { return _maxZ; }
    int asked = 0;

private:
    struct Key {
        int z, x, y;
        bool operator<(const Key& o) const
        {
            return z != o.z ? z < o.z : x != o.x ? x < o.x
                                                 : y < o.y;
        }
    };
    std::map<Key, bool> _has;
    uint8_t _who;
    int _minZ, _maxZ;
};
} // namespace
TEST_CASE("LayeredSource: the detail where it has the tile, the overview elsewhere")
{
    FakeSource detail('d', 10, 14), overview('o', 0, 10);
    detail.add(10, 5, 5);
    detail.add(12, 20, 20);
    overview.add(10, 5, 5);
    overview.add(10, 6, 6);
    overview.add(7, 1, 1);
    LayeredSource s(detail, &overview);
    std::vector<uint8_t> out;
    CHECK(s.minZoom() == 0);
    CHECK(s.maxZoom() == 14);
    /* both have it: the detail's */
    REQUIRE(s.getTile(10, 5, 5, out));
    CHECK(out[0] == 'd');
    /* only the detail's range has it */
    REQUIRE(s.getTile(12, 20, 20, out));
    CHECK(out[0] == 'd');
    /* outside the area of the detail, inside the overview's range: the overview's */
    REQUIRE(s.getTile(10, 6, 6, out));
    CHECK(out[0] == 'o');
    /* below the detail's range the detail is not even asked */
    const int asked = detail.asked;
    REQUIRE(s.getTile(7, 1, 1, out));
    CHECK(out[0] == 'o');
    CHECK(detail.asked == asked);
    /* nobody has it */
    CHECK_FALSE(s.getTile(12, 1, 1, out));
    CHECK_FALSE(s.getTile(3, 0, 0, out));
}
TEST_CASE("LayeredSource: without an overview it is the detail source")
{
    FakeSource detail('d', 13, 14);
    detail.add(13, 1, 2);
    LayeredSource s(detail, nullptr);
    std::vector<uint8_t> out;
    CHECK(s.minZoom() == 13);
    CHECK(s.maxZoom() == 14);
    REQUIRE(s.getTile(13, 1, 2, out));
    CHECK(out[0] == 'd');
    CHECK_FALSE(s.getTile(9, 1, 2, out));
}
