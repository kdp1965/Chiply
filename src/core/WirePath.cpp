#include "core/WirePath.h"

#include "core/JsonFormat.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace chiply {

namespace {

std::optional<double> parseNumber(const std::string& s, std::size_t pos)
{
    if (pos >= s.size())
        return std::nullopt;
    const char* begin = s.c_str() + pos;
    char* end = nullptr;
    double v = std::strtod(begin, &end);
    if (end == begin || *end != '\0')
        return std::nullopt;
    return v;
}

Point step(Point p, const Seg& s)
{
    if (s.axis == Axis::H)
        p.x += s.len;
    else
        p.y += s.len;
    return p;
}

void appendNormalized(std::vector<Seg>& out, const std::vector<Seg>& in)
{
    for (const Seg& s : in) {
        if (s.len == 0)
            continue;
        if (!out.empty() && out.back().axis == s.axis) {
            out.back().len = round2(out.back().len + s.len);
            if (out.back().len == 0)
                out.pop_back();
        } else {
            out.push_back(s);
        }
    }
}

} // namespace

std::optional<WirePath> parseWirePath(const std::vector<std::string>& items)
{
    WirePath p;
    for (const std::string& it : items) {
        if (it == "*") {
            if (p.hasStar)
                return std::nullopt;
            p.hasStar = true;
            continue;
        }
        if (it.empty() || (it[0] != 'h' && it[0] != 'v'))
            return std::nullopt;
        auto n = parseNumber(it, 1);
        if (!n)
            return std::nullopt;
        Seg s{it[0] == 'h' ? Axis::H : Axis::V, *n};
        (p.hasStar ? p.target : p.source).push_back(s);
    }
    return p;
}

std::vector<std::string> formatWirePath(const WirePath& path)
{
    std::vector<std::string> out;
    auto put = [&](const Seg& s) {
        out.push_back((s.axis == Axis::H ? "h" : "v") + formatNumber(s.len));
    };
    for (const Seg& s : path.source)
        put(s);
    if (path.hasStar) {
        out.emplace_back("*");
        for (const Seg& s : path.target)
            put(s);
    }
    return out;
}

WirePath normalized(const WirePath& path)
{
    WirePath r;
    appendNormalized(r.source, path.source);
    appendNormalized(r.target, path.target);
    r.hasStar = !r.target.empty();
    return r;
}

Point sourceRouteEnd(Point from, const WirePath& path)
{
    for (const Seg& s : path.source)
        from = step(from, s);
    return from;
}

std::vector<Point> routePolyline(Point from, Point to, const WirePath& path)
{
    // Mirrors the routing function in Wokwi's diagram editor: build a list of
    // relative h/v moves plus absolute points for the target side, close the
    // gap, then merge consecutive moves on the same axis (so an overshoot
    // followed by a step back collapses into one move), then walk the list.
    from = {round2(from.x), round2(from.y)};
    to = {round2(to.x), round2(to.y)};
    auto same = [](double a, double b) { return std::fabs(a - b) < 0.005; };

    struct Cmd {
        char kind; // 'h', 'v' (relative) or 'L' (absolute point)
        double a = 0, b = 0;
    };
    std::vector<Cmd> cmds;

    Point p = from;
    bool lastH = true;
    for (const Seg& s : path.source) {
        lastH = s.axis == Axis::H;
        cmds.push_back({lastH ? 'h' : 'v', s.len});
        p = step(p, s);
    }
    const std::size_t split = cmds.size();

    Point q = to;
    for (auto it = path.target.rbegin(); it != path.target.rend(); ++it) {
        lastH = it->axis == Axis::H;
        q = step(q, *it);
        cmds.insert(cmds.begin() + static_cast<long>(split), Cmd{'L', q.x, q.y});
    }

    if (!path.hasStar) {
        if (lastH && !same(p.x, q.x))
            cmds.push_back({'h', q.x - p.x});
        if (!same(p.y, q.y))
            cmds.push_back({'v', q.y - p.y});
        if (!lastH && !same(p.x, q.x))
            cmds.push_back({'h', q.x - p.x});
    } else if (!same(p.x, q.x) && !same(p.y, q.y)) {
        cmds.insert(cmds.begin() + static_cast<long>(split),
                    lastH ? Cmd{'h', q.x - p.x} : Cmd{'v', q.y - p.y});
        cmds.push_back({'L', to.x, to.y});
    } else if (path.hasStar) {
        // Wokwi stops at the last target-side point here; Chiply also draws
        // the final step into the pin so the wire visibly connects.
        cmds.push_back({'L', to.x, to.y});
    }

    for (std::size_t i = 1; i < cmds.size(); ++i) {
        if (cmds[i].kind != 'L' && cmds[i].kind == cmds[i - 1].kind) {
            cmds[i - 1].a = round2(cmds[i - 1].a + cmds[i].a);
            cmds.erase(cmds.begin() + static_cast<long>(i));
            --i;
        }
    }

    std::vector<Point> pts{from};
    Point c = from;
    for (const Cmd& k : cmds) {
        Point n = c;
        if (k.kind == 'h')
            n.x += k.a;
        else if (k.kind == 'v')
            n.y += k.a;
        else
            n = {k.a, k.b};
        if (!(n == c)) {
            pts.push_back(n);
            c = n;
        }
    }
    return pts;
}

std::vector<Point> simplifyPolyline(const std::vector<Point>& in)
{
    auto near = [](double a, double b) { return std::fabs(a - b) < 0.005; };
    std::vector<Point> pts;
    for (const Point& p : in)
        if (pts.empty() || !(near(pts.back().x, p.x) && near(pts.back().y, p.y)))
            pts.push_back(p);
    bool changed = true;
    while (changed && pts.size() > 2) {
        changed = false;
        for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
            const Point& a = pts[i - 1];
            const Point& b = pts[i];
            const Point& c = pts[i + 1];
            if ((near(a.x, b.x) && near(b.x, c.x)) || (near(a.y, b.y) && near(b.y, c.y))) {
                pts.erase(pts.begin() + static_cast<long>(i));
                changed = true;
                break;
            }
        }
    }
    return pts;
}

std::vector<Point> moveSegment(std::vector<Point> pts, std::size_t seg, double coord)
{
    if (seg + 1 >= pts.size())
        return pts;
    const bool horizontal = std::fabs(pts[seg].y - pts[seg + 1].y) < 0.005;
    if (seg == 0) {
        pts.insert(pts.begin(), pts.front()); // keep the source pin fixed
        ++seg;
    }
    if (seg + 2 == pts.size())
        pts.push_back(pts.back()); // keep the target pin fixed
    for (std::size_t k : {seg, seg + 1}) {
        if (horizontal)
            pts[k].y = coord;
        else
            pts[k].x = coord;
    }
    return simplifyPolyline(pts);
}

WirePath pathFromPolyline(const std::vector<Point>& in)
{
    const std::vector<Point> pts = simplifyPolyline(in);
    auto segs = [&](std::size_t count) {
        WirePath p;
        for (std::size_t i = 0; i < count && i + 1 < pts.size(); ++i) {
            const double dx = round2(pts[i + 1].x - pts[i].x);
            const double dy = round2(pts[i + 1].y - pts[i].y);
            if (std::fabs(dx) >= 0.005)
                p.source.push_back({Axis::H, dx});
            else if (std::fabs(dy) >= 0.005)
                p.source.push_back({Axis::V, dy});
        }
        return p;
    };
    if (pts.size() < 2)
        return {};
    auto matches = [&](const WirePath& p) {
        const auto r = simplifyPolyline(routePolyline(pts.front(), pts.back(), p));
        if (r.size() != pts.size())
            return false;
        for (std::size_t i = 0; i < r.size(); ++i)
            if (std::fabs(r[i].x - pts[i].x) > 0.02 || std::fabs(r[i].y - pts[i].y) > 0.02)
                return false;
        return true;
    };
    // Leave the last leg implicit when Wokwi's gap rule recreates it.
    WirePath shortForm = segs(pts.size() - 2);
    if (matches(shortForm))
        return shortForm;
    return segs(pts.size() - 1);
}

namespace {

// Moves pts[0] by d along one axis (H: x, V: y), keeping the polyline
// orthogonal and everything after the first segment on that axis fixed.
void stretchStartAxis(std::vector<Point>& pts, Axis axis, double d)
{
    if (std::fabs(d) < 0.005 || pts.empty())
        return;
    auto coord = [axis](Point& p) -> double& { return axis == Axis::H ? p.x : p.y; };
    auto other = [axis](const Point& p) { return axis == Axis::H ? p.y : p.x; };
    if (pts.size() == 1) {
        coord(pts[0]) += d;
        return;
    }
    // First segment that runs along the move axis.
    std::size_t k = pts.size();
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        if (std::fabs(other(pts[i]) - other(pts[i + 1])) < 0.005 && std::fabs(coord(pts[i]) - coord(pts[i + 1])) >= 0.005) {
            k = i;
            break;
        }
    }
    if (k < pts.size()) {
        for (std::size_t j = 0; j <= k; ++j)
            coord(pts[j]) += d; // segments before k slide rigidly, k stretches
        return;
    }
    // Only perpendicular segments: add a jog half-way so the far end stays.
    const Point a = pts.front(), b = pts.back();
    double mid = (other(a) + other(b)) / 2;
    const double g = 9.6;
    const double snapped = std::round(mid / g) * g;
    if (std::fabs(snapped - other(a)) >= 0.005 && std::fabs(snapped - other(b)) >= 0.005)
        mid = snapped;
    Point a2 = a, j1, j2;
    coord(a2) += d;
    if (axis == Axis::H) {
        j1 = {a2.x, mid};
        j2 = {b.x, mid};
    } else {
        j1 = {mid, a2.y};
        j2 = {mid, b.y};
    }
    pts = {a2, j1, j2, b};
}

} // namespace

std::vector<Point> stretchEnd(const std::vector<Point>& in, bool atStart, Point newEnd)
{
    std::vector<Point> pts = simplifyPolyline(in);
    if (pts.empty())
        return pts;
    if (!atStart)
        std::reverse(pts.begin(), pts.end());
    const double dx = round2(newEnd.x - pts[0].x), dy = round2(newEnd.y - pts[0].y);
    stretchStartAxis(pts, Axis::H, dx);
    pts = simplifyPolyline(pts);
    stretchStartAxis(pts, Axis::V, dy);
    pts[0] = newEnd;
    pts = simplifyPolyline(pts);
    if (!atStart)
        std::reverse(pts.begin(), pts.end());
    return pts;
}

} // namespace chiply
