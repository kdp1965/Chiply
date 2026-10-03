#include "core/Geometry.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace chiply {

Point partToDiagram(const Part& part, const PartDef& def, Point local)
{
    const double cx = def.width / 2, cy = def.height / 2;
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
