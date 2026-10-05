#include "core/BusRoute.h"

#include "core/JsonFormat.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>

namespace chiply {

namespace {

constexpr double kEps = 0.005;

struct LeadSeg {
    Point a, b;
    int dx = 0, dy = 0; // unit direction
    bool horizontal() const { return dx != 0; }
};

// The lead as segments. The first one always runs along the bus's first
// axis (zero length, direction `firstSign`, if the lead starts the other
// way), so every wire leaves its pin at right angles to the arrangement.
std::vector<LeadSeg> segments(const std::vector<Point>& leadIn, bool firstHorizontal, int firstSign)
{
    const std::vector<Point> lead = simplifyPolyline(leadIn);
    std::vector<LeadSeg> segs;
    for (std::size_t i = 0; i + 1 < lead.size(); ++i) {
        LeadSeg s{lead[i], lead[i + 1], 0, 0};
        const double ddx = s.b.x - s.a.x, ddy = s.b.y - s.a.y;
        if (std::fabs(ddx) >= std::fabs(ddy))
            s.dx = ddx >= 0 ? 1 : -1;
        else
            s.dy = ddy >= 0 ? 1 : -1;
        segs.push_back(s);
    }
    const int sign = firstSign < 0 ? -1 : 1;
    if (!lead.empty() && (segs.empty() || segs.front().horizontal() != firstHorizontal)) {
        LeadSeg z{lead.front(), lead.front(), 0, 0};
        (firstHorizontal ? z.dx : z.dy) = sign;
        segs.insert(segs.begin(), z);
    }
    return segs;
}

// Tracks to the left of the travel direction (screen coordinates, y down)
// for a wire `rank` pins along the arrangement axis from the lead.
int sideOf(const LeadSeg& first, bool firstHorizontal, int rank)
{
    // Left normal of (dx, dy) is (dy, -dx); its component along the axis.
    const int sigma = firstHorizontal ? -first.dx : first.dy;
    return rank * sigma;
}

// The line a wire runs on for segment j: y for a horizontal segment, x for
// a vertical one.
double lineOf(const std::vector<LeadSeg>& segs, std::size_t j, Point start, int side, double grid)
{
    const LeadSeg& s = segs[j];
    if (j == 0)
        return s.horizontal() ? start.y : start.x;
    // Left normal (dy, -dx).
    return s.horizontal() ? s.a.y + side * grid * double(-s.dx) : s.a.x + side * grid * double(s.dy);
}

// start, then the corners between segments 0..upTo (corner j joins j-1 and j).
std::vector<Point> corners(const std::vector<LeadSeg>& segs, std::size_t upTo, Point start, int side, double grid)
{
    std::vector<Point> pts{start};
    for (std::size_t j = 1; j <= upTo && j < segs.size(); ++j) {
        const double prev = lineOf(segs, j - 1, start, side, grid), cur = lineOf(segs, j, start, side, grid);
        if (segs[j - 1].horizontal() != segs[j].horizontal()) {
            pts.push_back(segs[j].horizontal() ? Point{prev, cur} : Point{cur, prev});
        } else if (segs[j].horizontal()) { // a reversal: step across at the lead's corner
            pts.push_back({segs[j].a.x, prev});
            pts.push_back({segs[j].a.x, cur});
        } else {
            pts.push_back({prev, segs[j].a.y});
            pts.push_back({cur, segs[j].a.y});
        }
    }
    return pts;
}

// Where a wire is on segment j when the lead is at that segment's end.
Point endOn(const std::vector<LeadSeg>& segs, std::size_t j, Point start, int side, double grid)
{
    const double line = lineOf(segs, j, start, side, grid);
    return segs[j].horizontal() ? Point{segs[j].b.x, line} : Point{line, segs[j].b.y};
}

std::vector<Point> rounded(std::vector<Point> pts)
{
    for (Point& p : pts)
        p = {round2(p.x), round2(p.y)};
    return pts;
}

} // namespace

BusOrder busOrder(const std::vector<Point>& pins, std::size_t lead)
{
    BusOrder o;
    if (pins.empty() || lead >= pins.size())
        return o;
    double minX = pins[0].x, maxX = pins[0].x, minY = pins[0].y, maxY = pins[0].y;
    for (const Point& p : pins) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    o.column = (maxY - minY) >= (maxX - minX);
    std::vector<std::size_t> sorted(pins.size());
    std::iota(sorted.begin(), sorted.end(), std::size_t(0));
    std::stable_sort(sorted.begin(), sorted.end(), [&](std::size_t a, std::size_t b) {
        const double ka = o.column ? pins[a].y : pins[a].x, kb = o.column ? pins[b].y : pins[b].x;
        if (std::fabs(ka - kb) >= kEps)
            return ka < kb;
        return (o.column ? pins[a].x : pins[a].y) < (o.column ? pins[b].x : pins[b].y);
    });
    std::vector<int> pos(pins.size());
    for (std::size_t i = 0; i < sorted.size(); ++i)
        pos[sorted[i]] = int(i);
    o.rank.resize(pins.size());
    for (std::size_t i = 0; i < pins.size(); ++i)
        o.rank[i] = pos[i] - pos[lead];
    o.order = sorted;
    std::stable_sort(o.order.begin(), o.order.end(), [&](std::size_t a, std::size_t b) {
        const int da = std::abs(o.rank[a]), db = std::abs(o.rank[b]);
        return da != db ? da < db : o.rank[a] < o.rank[b];
    });
    return o;
}

std::vector<Point> busWirePath(const std::vector<Point>& lead, bool firstHorizontal, int firstSign, Point start,
                               int rank, double grid)
{
    const std::vector<LeadSeg> segs = segments(lead, firstHorizontal, firstSign);
    if (segs.empty())
        return {start};
    const int side = sideOf(segs.front(), firstHorizontal, rank);
    std::vector<Point> pts = corners(segs, segs.size() - 1, start, side, grid);
    pts.push_back(endOn(segs, segs.size() - 1, start, side, grid));
    return simplifyPolyline(rounded(pts));
}

BusFanOut busFanOut(const std::vector<Point>& leadFinal, bool firstHorizontal, int firstSign, Point start, int rank,
                    double grid)
{
    BusFanOut f;
    f.legHorizontal = firstHorizontal;
    f.fixed = {start};
    f.stub = {start};
    const std::vector<LeadSeg> segs = segments(leadFinal, firstHorizontal, firstSign);
    if (segs.size() < 2)
        return f; // the lead went straight to its pin: no track to share
    // The last segment is the lead's own turn into its pin; the one before
    // it is the last shared track.
    const std::size_t track = segs.size() - 2;
    const int side = sideOf(segs.front(), firstHorizontal, rank);
    std::vector<Point> pts = corners(segs, track, start, side, grid);
    f.legHorizontal = segs[track].horizontal();
    f.fixed = rounded(pts);
    pts.push_back(endOn(segs, track, start, side, grid));
    f.stub = simplifyPolyline(rounded(pts));
    // Drop duplicate points but keep the corners (the last fixed point is
    // where the wire enters its track).
    std::vector<Point> fixed;
    for (const Point& p : f.fixed)
        if (fixed.empty() || std::fabs(fixed.back().x - p.x) >= kEps || std::fabs(fixed.back().y - p.y) >= kEps)
            fixed.push_back(p);
    f.fixed = fixed;
    return f;
}

} // namespace chiply
