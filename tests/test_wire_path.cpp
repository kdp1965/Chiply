#include "core/WirePath.h"

#include <catch2/catch_test_macros.hpp>

using namespace chiply;

TEST_CASE("parse and format source-anchored paths")
{
    std::vector<std::string> in{"h21.01", "v-28.8", "h96"};
    auto p = parseWirePath(in);
    REQUIRE(p);
    CHECK(p->source.size() == 3);
    CHECK(p->source[1] == Seg{Axis::V, -28.8});
    CHECK_FALSE(p->hasStar);
    CHECK(formatWirePath(*p) == in);
}

TEST_CASE("star splits source and target moves")
{
    std::vector<std::string> in{"v10", "h5", "*", "v-15", "h10"};
    auto p = parseWirePath(in);
    REQUIRE(p);
    CHECK(p->hasStar);
    CHECK(p->source.size() == 2);
    CHECK(p->target.size() == 2);
    CHECK(formatWirePath(*p) == in);
}

TEST_CASE("bad items are rejected")
{
    CHECK_FALSE(parseWirePath({"x10"}));
    CHECK_FALSE(parseWirePath({"h"}));
    CHECK_FALSE(parseWirePath({"h1", "*", "v1", "*"}));
    CHECK_FALSE(parseWirePath({"h1.5px"}));
    CHECK(parseWirePath({}));
}

TEST_CASE("v0 survives a round trip")
{
    auto p = parseWirePath({"v0"});
    REQUIRE(p);
    CHECK(formatWirePath(*p) == std::vector<std::string>{"v0"});
}

TEST_CASE("normalization merges and drops zero moves")
{
    auto p = parseWirePath({"h10", "h5", "v0", "v9.6", "v-9.6", "h-3"});
    REQUIRE(p);
    CHECK(formatWirePath(normalized(*p)) == std::vector<std::string>{"h12"});
}

TEST_CASE("polyline walks source moves, gap, then target moves backwards")
{
    auto p = parseWirePath({"h10", "*", "v-20"});
    REQUIRE(p);
    auto pts = routePolyline({0, 0}, {50, 40}, *p);
    REQUIRE(pts.size() == 4);
    CHECK(pts[0] == Point{0, 0});
    CHECK(pts[1] == Point{10, 0});
    CHECK(pts[2] == Point{50, 20});
    CHECK(pts[3] == Point{50, 40});
}
