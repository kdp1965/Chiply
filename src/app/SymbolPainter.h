#pragma once
// Chiply's own artwork for every part symbol, drawn in the part's local,
// unrotated coordinates (px, origin at the outline's top-left). Logic
// symbols follow the Wokwi look: magenta body, black leads (theme colors).
#include "Theme.h"
#include "core/Document.h"
#include "core/PartLibrary.h"

class QPainter;

namespace SymbolPainter {

// Live state while simulating (PLAN.md 6.3). Meaning of `bits` per part:
// pushbutton bit 0 = pressed; slide switch bit 0 = lever on the pin-3 side;
// DIP switch bits 0..7 = switches on; LED bit 0 = lit; 7-segment bits 0..7 =
// segments A..G, DP lit; flip-flop bit 0 = Q is 1, bit 1 = Q unknown (X/Z).
struct SimVisual {
    unsigned bits = 0;
};

// `sim` is null when not simulating (edit mode drawing).
void paint(QPainter* p, const chiply::PartDef& def, const chiply::Part& part, const CanvasColors& colors,
           const SimVisual* sim = nullptr);

// Fallback for part types the library does not know.
void paintUnknown(QPainter* p, double w, double h, const QString& type, const CanvasColors& colors);

} // namespace SymbolPainter
