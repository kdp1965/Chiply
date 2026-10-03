#include "core/Geometry.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace chiply {

Point partToDiagram(const Part& part, const PartDef& def, Point local)
{
    // Wokwi rotates about the center of the element's layout box, whose size
    // the browser reports in whole pixels (offsetWidth/offsetHeight). Using
    // the rounded size reproduces Wokwi's pin positions to the 0.01 px.
    const double cx = std::round(def.width) / 2, cy = std::round(def.height) / 2;
    double dx = local.x - cx, dy = local.y - cy;
    int quarter = ((part.rotate % 360) + 360) % 360;
    if (quarter % 90 == 0) {
        for (int i = 0; i < quarter / 90; ++i) {
            const double t = dx;
            dx = -dy; // clockwise in y-down coordinates
            dy = t;
        }
    } else {
        const double a = quarter * std::numbers::pi / 180.0;
        const double rx = dx * std::cos(a) - dy * std::sin(a);
        const double ry = dx * std::sin(a) + dy * std::cos(a);
        dx = rx;
        dy = ry;
    }
    return {part.left + cx + dx, part.top + cy + dy};
}

std::optional<Point> pinPosition(const Part& part, const PartDef& def, const std::string& pin)
{
    const PinDef* p = def.findPin(pin);
    if (!p)
        return std::nullopt;
    return partToDiagram(part, def, {p->x, p->y});
}

Rect partBounds(const Part& part, const PartDef& def)
{
    const Point c[4] = {
        partToDiagram(part, def, {0, 0}),
        partToDiagram(part, def, {def.width, 0}),
        partToDiagram(part, def, {0, def.height}),
        partToDiagram(part, def, {def.width, def.height}),
    };
    double x0 = c[0].x, x1 = c[0].x, y0 = c[0].y, y1 = c[0].y;
    for (const Point& p : c) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

Point snapPinOffset(const Part& part, const PartDef* def)
{
    if (!def || def->pins.empty())
        return {0, 0};
    const Point p = partToDiagram(part, *def, {def->pins.front().x, def->pins.front().y});
    return {p.x - part.left, p.y - part.top};
}

Point snapPlacement(const Part& part, const PartDef* def, double left, double top, double grid)
{
    if (grid <= 0)
        return {left, top};
    const Point off = snapPinOffset(part, def);
    const double px = std::round((left + off.x) / grid) * grid;
    const double py = std::round((top + off.y) / grid) * grid;
    auto r2 = [](double v) {
        const double r = std::round(v * 100.0) / 100.0;
        return r == 0.0 ? 0.0 : r;
    };
    return {r2(px - off.x), r2(py - off.y)};
}

std::optional<Point> pinPosition(const Document& doc, const PartLibrary& lib, const PinRef& ref)
{
    const Part* part = doc.findPart(ref.part);
    if (!part)
        return std::nullopt;
    const PartDef* def = lib.find(part->type);
    if (!def)
        return std::nullopt;
    return pinPosition(*part, *def, ref.pin);
}

} // namespace chiply
