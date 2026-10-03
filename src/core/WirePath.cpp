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

} // namespace chiply
