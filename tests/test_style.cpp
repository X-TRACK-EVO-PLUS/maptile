#include "maptile/style.h"
#include "test_common.h"
#include <cstring>

using namespace MapTile;

TEST_CASE("Stops interpolate and clamp")
{
    Stops s;
    CHECK(s.at(10) == doctest::Approx(1.0f)); /* constant when empty */
    s.stops = { { 10, 1 }, { 14, 5 }, { 16, 9 } };
    CHECK(s.at(5) == doctest::Approx(1));
    CHECK(s.at(12) == doctest::Approx(3));
    CHECK(s.at(15) == doctest::Approx(7));
    CHECK(s.at(20) == doctest::Approx(9));
}

TEST_CASE("Style: loads a minimal style")
{
    Style st;
    const char* json = R"({
        "schema": 2, "name": "t",
        "palette": { "water": "#a8c2d0" },
        "background": "#ffffff",
        "layers": [
          { "id": "w", "source": "water_polygons", "type": "fill", "color": "$water" },
          { "id": "r", "source": "streets", "type": "line", "color": "#000", "width": [[10, 1], [16, 5]],
            "filter": { "kind": ["primary", "trunk"], "!tunnel": ["true"] }, "dash": [3, 2], "min_zoom": 9 }
        ],
        "labels": [ { "id": "n", "source": "street_labels", "color": "#112233", "size": 12, "weight": 700 } ]
    })";
    REQUIRE(st.loadString(json, strlen(json)));
    CHECK(st.name() == "t");
    CHECK(st.background() == Color::rgb(0xffffff));
    REQUIRE(st.layers().size() == 2);
    CHECK(st.layers()[0].color.value == Color::rgb(0xa8c2d0));
    const StyleLayer& r = st.layers()[1];
    CHECK(r.kind == StyleLayer::LINE);
    CHECK(r.filter.terms.size() == 2);
    CHECK(r.filter.terms[1].negate);
    CHECK(r.filter.terms[1].key == "tunnel");
    CHECK(r.dash.size() == 2);
    CHECK(r.width.at(13) == doctest::Approx(3));
    CHECK(r.minZoom == doctest::Approx(9));
    REQUIRE(st.labels().size() == 1);
    CHECK(st.labels()[0].weight == 700);
}

TEST_CASE("Style: palette overrides re-theme without reloading")
{
    Style st;
    const char* json = R"({ "schema": 2, "palette": { "land": "#e6e4dd" }, "background": "$land",
        "layers": [ { "source": "land", "type": "fill", "color": "$land" } ] })";
    REQUIRE(st.loadString(json, strlen(json)));
    CHECK(st.background() == Color::rgb(0xe6e4dd));
    st.setColor("land", Color::rgb(0x0e0e11));
    CHECK(st.background() == Color::rgb(0x0e0e11));
    CHECK(st.layers()[0].color.value == Color::rgb(0x0e0e11));
    /* an override made before loading survives the load */
    Style st2;
    st2.setColor("land", Color::rgb(0x112233));
    REQUIRE(st2.loadString(json, strlen(json)));
    CHECK(st2.background() == Color::rgb(0x112233));
}

TEST_CASE("Style: errors say what is wrong")
{
    Style st;
    CHECK_FALSE(st.loadString("{ nope"));
    CHECK(st.error().find("style JSON") != std::string::npos);
    CHECK_FALSE(st.loadString(R"({ "layers": [] })"));
    CHECK(st.error().find("schema") != std::string::npos);
    CHECK_FALSE(st.loadString(R"({ "schema": 2, "layers": [ { "source": "x", "type": "circle" } ] })"));
    CHECK(st.error().find("fill or line") != std::string::npos);
    CHECK_FALSE(st.loadString(R"({ "schema": 2, "layers": [ { "source": "x", "type": "fill", "color": "red" } ] })"));
    CHECK(st.error().find("bad colour") != std::string::npos);
}

TEST_CASE("Style: JSON \\u escapes become UTF-8")
{
    Style st;
    const char* json = R"({ "schema": 2, "name": "\u5730\u56fe \ud83d\uddfa" })";
    REQUIRE(st.loadString(json, strlen(json)));
    CHECK(st.name() == "\xe5\x9c\xb0\xe5\x9b\xbe \xf0\x9f\x97\xba");
}

TEST_CASE("The bundled light style loads")
{
    Style st;
    REQUIRE_MESSAGE(st.loadFile("tests/data/style-light.json"), st.error());
    CHECK(st.layers().size() > 10);
    CHECK(st.labels().size() >= 4);
}

TEST_CASE("Color::parse")
{
    Color c;
    CHECK(Color::parse("#fff", c));
    CHECK(c == Color::rgb(0xffffff));
    CHECK(Color::parse("#11223380", c));
    CHECK(c.a == 0x80);
    CHECK_FALSE(Color::parse("112233", c));
    CHECK_FALSE(Color::parse("#12345", c));
    CHECK_FALSE(Color::parse("#12345g", c));
}
