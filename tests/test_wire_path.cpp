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

TEST_CASE("gap continues on the last move's axis, then turns")
{
    auto h = parseWirePath({"h10"});
    REQUIRE(h);
    // h10 and the closing h40 merge into one move.
    CHECK(routePolyline({0, 0}, {50, 40}, *h) == std::vector<Point>{{0, 0}, {50, 0}, {50, 40}});
    auto v = parseWirePath({"v10"});
    REQUIRE(v);
    CHECK(routePolyline({0, 0}, {50, 40}, *v) == std::vector<Point>{{0, 0}, {0, 40}, {50, 40}});
    auto hv = parseWirePath({"h10", "v10"});
    REQUIRE(hv);
    CHECK(routePolyline({0, 0}, {50, 40}, *hv)
          == std::vector<Point>{{0, 0}, {10, 0}, {10, 40}, {50, 40}});
}

TEST_CASE("empty path goes horizontal first")
{
    CHECK(routePolyline({0, 0}, {50, 40}, WirePath{})
          == std::vector<Point>{{0, 0}, {50, 0}, {50, 40}});
}

TEST_CASE("v0 to a pin on the same row is one straight segment")
{
    auto p = parseWirePath({"v0"});
    REQUIRE(p);
    CHECK(routePolyline({0, 5}, {30, 5}, *p) == std::vector<Point>{{0, 5}, {30, 5}});
}

TEST_CASE("star: target moves walk from the target, last item first")
{
    // From the docs example: v10 h5 from the source; from the target h10
    // first, then v-15.
    auto p = parseWirePath({"v10", "h5", "*", "v-15", "h10"});
    REQUIRE(p);
    auto pts = routePolyline({0, 0}, {100, 100}, *p);
    CHECK(pts == std::vector<Point>{{0, 0}, {0, 10}, {5, 10}, {5, 85}, {110, 85}, {110, 100}, {100, 100}});
}

TEST_CASE("endpoints are rounded to 2 decimals")
{
    auto pts = routePolyline({0.004, 0}, {10.006, 0}, WirePath{});
    CHECK(pts == std::vector<Point>{{0, 0}, {10.01, 0}});
}

TEST_CASE("an overshoot followed by the closing step merges into one move")
{
    // or211:OUT -> or218:A in the reference design: h57.6 overshoots the
    // target column by 28.8; Wokwi merges it with the closing h-28.8.
    auto p = parseWirePath({"v0", "h57.6"});
    REQUIRE(p);
    CHECK(routePolyline({-2524.8, -67.2}, {-2496, 240}, *p)
          == std::vector<Point>{{-2524.8, -67.2}, {-2496, -67.2}, {-2496, 240}});
}
