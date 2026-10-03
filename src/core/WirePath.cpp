#include "core/WirePath.h"

#include "core/JsonFormat.h"

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
    from = {round2(from.x), round2(from.y)};
    to = {round2(to.x), round2(to.y)};
    auto same = [](double a, double b) { return std::fabs(a - b) < 0.005; };

    std::vector<Point> pts{from};
    Point p = from;
    bool lastH = true;
    for (const Seg& s : path.source) {
        lastH = s.axis == Axis::H;
        p = step(p, s);
        pts.push_back(p);
    }

    if (!path.hasStar) {
        if (lastH && !same(p.x, to.x))
            pts.push_back(p = {to.x, p.y});
        if (!same(p.y, to.y))
            pts.push_back(p = {p.x, to.y});
        if (!lastH && !same(p.x, to.x))
            pts.push_back(p = {to.x, p.y});
    } else {
        // Walk target moves from the target pin, last item first.
        std::vector<Point> tail{to};
        Point q = to;
        for (auto it = path.target.rbegin(); it != path.target.rend(); ++it) {
            lastH = it->axis == Axis::H;
            q = step(q, *it);
            tail.push_back(q);
        }
        if (!same(p.x, q.x) && !same(p.y, q.y))
            pts.push_back(lastH ? Point{q.x, p.y} : Point{p.x, q.y});
        pts.insert(pts.end(), tail.rbegin(), tail.rend());
    }
    if (!(pts.back() == to))
        pts.push_back(to);

    std::vector<Point> out;
    for (const Point& pt : pts)
        if (out.empty() || !(out.back() == pt))
            out.push_back(pt);
    return out;
}

} // namespace chiply
