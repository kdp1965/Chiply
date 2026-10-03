#pragma once
// Wokwi wire routing mini-language: a list of "h<px>" / "v<px>" moves from
// the source pin, optionally followed by "*" and moves applied from the target
// pin (last item first). The remaining gap is closed with orthogonal legs
// using Wokwi's rule, see routePolyline().
#include <optional>
#include <string>
#include <vector>

namespace chiply {

struct Point {
    double x = 0, y = 0;
    bool operator==(const Point&) const = default;
};

enum class Axis { H, V };

struct Seg {
    Axis axis = Axis::H;
    double len = 0;
    bool operator==(const Seg&) const = default;
};

struct WirePath {
    std::vector<Seg> source;  // moves starting at the source pin
    std::vector<Seg> target;  // moves after "*", in file order; the last one
                              // starts at the target pin
    bool hasStar = false;
    bool operator==(const WirePath&) const = default;
};

// Returns nullopt if any item is not "h<num>", "v<num>" or "*", or "*"
// appears more than once.
std::optional<WirePath> parseWirePath(const std::vector<std::string>& items);
std::vector<std::string> formatWirePath(const WirePath& path);

// Merges consecutive moves on the same axis and drops zero-length moves.
// Used after editing; loaded paths are never normalized implicitly.
WirePath normalized(const WirePath& path);

// Polyline from source pin to target pin exactly as Wokwi draws it:
//  - endpoints rounded to 2 decimals;
//  - source moves from `from`;
//  - without "*": the gap is closed on the axis of the last source move first
//    (horizontal when there are no moves), then the other axis;
//  - with "*": target moves are walked from `to` (last item first); if both
//    axes still differ, one leg on the axis of the first move after "*" is
//    added, then a straight line to the target-side point;
//  - consecutive moves on the same axis are merged (summed), so a recorded
//    overshoot and the step back collapse into a single move.
// Verified against every wire of the reference design as rendered by Wokwi.
std::vector<Point> routePolyline(Point from, Point to, const WirePath& path);

// Removes consecutive duplicate points and interior points that lie on a
// straight line between their neighbours.
std::vector<Point> simplifyPolyline(const std::vector<Point>& pts);

// Moves segment `seg` (between pts[seg] and pts[seg+1]) of an orthogonal
// polyline perpendicular to itself so that it lies at `coord` (its new y if
// horizontal, x if vertical). Neighbouring segments stretch; a segment that
// touches an endpoint gets a new connecting segment so the ends stay put.
std::vector<Point> moveSegment(std::vector<Point> pts, std::size_t seg, double coord);

// A source-anchored Wokwi path whose routePolyline() from pts.front() to
// pts.back() reproduces `pts` (an orthogonal polyline). Moves are rounded to
// 2 decimals; the final leg is left implicit, as Wokwi's editor writes it.
WirePath pathFromPolyline(const std::vector<Point>& pts);

// Where the source-side moves end (the start of the auto-completed tail when
// there are no target-side moves).
Point sourceRouteEnd(Point from, const WirePath& path);

} // namespace chiply
