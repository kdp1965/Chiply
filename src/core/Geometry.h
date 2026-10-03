#pragma once
// Placement geometry, matching Wokwi: a part's `left`/`top` is its unrotated
// top-left corner, and `rotate` (degrees, clockwise) turns it about the
// center of its layout box (CSS transform-origin: center), i.e. the outline
// size rounded to whole pixels.
#include "core/Document.h"
#include "core/PartLibrary.h"
#include "core/WirePath.h"

#include <optional>

namespace chiply {

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
};

// Rotates a part-local point (unrotated, relative to top-left) into diagram
// coordinates.
Point partToDiagram(const Part& part, const PartDef& def, Point local);

// Diagram position of a pin; nullopt if the pin does not exist on the part.
std::optional<Point> pinPosition(const Part& part, const PartDef& def, const std::string& pin);

// Axis-aligned bounding box of the part's rotated outline.
Rect partBounds(const Part& part, const PartDef& def);

// Resolves a wire end through a library; nullopt for unknown parts/pins.
std::optional<Point> pinPosition(const Document& doc, const PartLibrary& lib, const PinRef& ref);

} // namespace chiply
