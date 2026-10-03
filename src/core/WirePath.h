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

// Moves interior corner `i` of an orthogonal polyline to `to`; the two
// segments meeting there follow so everything stays orthogonal. A neighbour
// that is an endpoint (pin) stays put and gets a connecting segment.
std::vector<Point> moveCorner(std::vector<Point> pts, std::size_t i, Point to);

// Splits segment `seg` at the point of it nearest `at` and drags the far
// half sideways: the split slides along the segment to `to`'s along-axis
// coordinate, and the part from the split onwards moves perpendicular to
// `to`'s other coordinate, leaving a step. Returns the simplified result.
std::vector<Point> splitSegment(const std::vector<Point>& pts, std::size_t seg, Point to);

// Index of the segment of `pts` nearest `p`, and that distance.
std::pair<std::size_t, double> nearestSegment(const std::vector<Point>& pts, Point p);

// Elastic end move: the endpoint at the start (atStart) or end of an
// orthogonal polyline moves to `newEnd`; the rest of the route stays where it
// is in the diagram. For the horizontal part of the move, the first
// horizontal segment from that end stretches and any vertical segments
// before it slide sideways with the end; likewise for the vertical part
// with the first vertical segment. If there is no segment on that axis, a
// jog is inserted half-way along the route.
std::vector<Point> stretchEnd(const std::vector<Point>& pts, bool atStart, Point newEnd);

// A source-anchored Wokwi path whose routePolyline() from pts.front() to
// pts.back() reproduces `pts` (an orthogonal polyline). Moves are rounded to
// 2 decimals; the final leg is left implicit, as Wokwi's editor writes it.
WirePath pathFromPolyline(const std::vector<Point>& pts);

// Where the source-side moves end (the start of the auto-completed tail when
// there are no target-side moves).
Point sourceRouteEnd(Point from, const WirePath& path);

} // namespace chiply
