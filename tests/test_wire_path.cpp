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

TEST_CASE("simplify drops duplicates and collinear points")
{
    CHECK(simplifyPolyline({{0, 0}, {0, 0}, {10, 0}, {20, 0}, {20, 5}})
          == std::vector<Point>{{0, 0}, {20, 0}, {20, 5}});
}

TEST_CASE("moving a middle segment stretches its neighbours")
{
    std::vector<Point> pts{{0, 0}, {10, 0}, {10, 50}, {40, 50}};
    // The vertical segment at x=10 moves to x=28.8.
    CHECK(moveSegment(pts, 1, 28.8) == std::vector<Point>{{0, 0}, {28.8, 0}, {28.8, 50}, {40, 50}});
}

TEST_CASE("moving an end segment keeps the pin and adds a jog")
{
    std::vector<Point> pts{{0, 0}, {30, 0}, {30, 40}};
    // First (horizontal) segment moves down to y=9.6: pin stays at (0,0).
    CHECK(moveSegment(pts, 0, 9.6) == std::vector<Point>{{0, 0}, {0, 9.6}, {30, 9.6}, {30, 40}});
    // A straight wire becomes a dog-leg.
    CHECK(moveSegment({{0, 0}, {50, 0}}, 0, -19.2)
          == std::vector<Point>{{0, 0}, {0, -19.2}, {50, -19.2}, {50, 0}});
}

TEST_CASE("pathFromPolyline reproduces the polyline through Wokwi's rules")
{
    const std::vector<std::vector<Point>> cases{
        {{0, 0}, {50, 0}},
        {{0, 0}, {0, 30}},
        {{0, 0}, {28.8, 0}, {28.8, 50}, {40, 50}},
        {{0, 0}, {0, 9.6}, {30, 9.6}, {30, 40}},
        {{0, 0}, {0, -19.2}, {50, -19.2}, {50, 0}},
        {{5, 5}, {-20, 5}, {-20, 80}, {60, 80}, {60, 40}, {90, 40}},
    };
    for (const auto& pts : cases) {
        WirePath p = pathFromPolyline(pts);
        CHECK(simplifyPolyline(routePolyline(pts.front(), pts.back(), p)) == pts);
        CHECK_FALSE(p.hasStar);
    }
    // The last leg is left implicit, like Wokwi's editor writes paths.
    CHECK(formatWirePath(pathFromPolyline({{0, 0}, {28.8, 0}, {28.8, 50}, {40, 50}}))
          == std::vector<std::string>{"h28.8", "v50"});
}

TEST_CASE("elastic: horizontal first segment stretches on a horizontal move")
{
    // Part pin at (0,0) -> right 30 -> down 100 -> right to (80,100).
    std::vector<Point> pts{{0, 0}, {30, 0}, {30, 100}, {80, 100}};
    // Move the part (source end) right by 9.6: only the first segment changes.
    CHECK(stretchEnd(pts, true, {9.6, 0}) == std::vector<Point>{{9.6, 0}, {30, 0}, {30, 100}, {80, 100}});
    // Moving left also leaves the long vertical where it is.
    CHECK(stretchEnd(pts, true, {-9.6, 0}) == std::vector<Point>{{-9.6, 0}, {30, 0}, {30, 100}, {80, 100}});
}

TEST_CASE("elastic: a segment at 90 degrees slides, the next one stretches")
{
    // Pin leaves vertically: (0,0) down 40, right to (60,40).
    std::vector<Point> pts{{0, 0}, {0, 40}, {60, 40}};
    CHECK(stretchEnd(pts, true, {9.6, 0}) == std::vector<Point>{{9.6, 0}, {9.6, 40}, {60, 40}});
    // A vertical move stretches the vertical segment.
    CHECK(stretchEnd(pts, true, {0, 9.6}) == std::vector<Point>{{0, 9.6}, {0, 40}, {60, 40}});
}

TEST_CASE("elastic: vertical move on a horizontal-first wire")
{
    std::vector<Point> pts{{0, 0}, {30, 0}, {30, 100}, {80, 100}};
    // The horizontal stub slides down; the vertical segment shortens.
    CHECK(stretchEnd(pts, true, {0, 9.6}) == std::vector<Point>{{0, 9.6}, {30, 9.6}, {30, 100}, {80, 100}});
}

TEST_CASE("elastic: target end and straight wires")
{
    std::vector<Point> pts{{0, 0}, {30, 0}, {30, 100}, {80, 100}};
    CHECK(stretchEnd(pts, false, {89.6, 100}) == std::vector<Point>{{0, 0}, {30, 0}, {30, 100}, {89.6, 100}});
    // A straight horizontal wire just gets longer/shorter.
    CHECK(stretchEnd({{0, 0}, {50, 0}}, true, {9.6, 0}) == std::vector<Point>{{9.6, 0}, {50, 0}});
    // A straight vertical wire moved sideways gets a jog half-way.
    CHECK(stretchEnd({{0, 0}, {0, 96}}, true, {9.6, 0})
          == std::vector<Point>{{9.6, 0}, {9.6, 48}, {0, 48}, {0, 96}});
}

TEST_CASE("elastic result is a valid Wokwi path")
{
    std::vector<Point> pts{{0, 0}, {0, 40}, {60, 40}, {60, 90}, {120, 90}};
    for (Point e : {Point{9.6, 0}, Point{-19.2, 9.6}, Point{0, -28.8}}) {
        auto moved = stretchEnd(pts, true, e);
        WirePath p = pathFromPolyline(moved);
        CHECK(simplifyPolyline(routePolyline(moved.front(), moved.back(), p)) == moved);
    }
}
