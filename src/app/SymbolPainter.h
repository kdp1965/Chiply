#pragma once
// Chiply's own artwork for every part symbol, drawn in the part's local,
// unrotated coordinates (px, origin at the outline's top-left). Logic
// symbols follow the Wokwi look: magenta body, black leads (theme colors).
#include "Theme.h"
#include "core/Document.h"
#include "core/PartLibrary.h"

class QPainter;

namespace SymbolPainter {

void paint(QPainter* p, const chiply::PartDef& def, const chiply::Part& part, const CanvasColors& colors);

// Fallback for part types the library does not know.
void paintUnknown(QPainter* p, double w, double h, const QString& type, const CanvasColors& colors);

} // namespace SymbolPainter
