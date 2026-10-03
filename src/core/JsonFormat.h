#pragma once
#include "core/Json.h"

#include <string>

namespace chiply {

// Formats a number the way JavaScript's JSON.stringify does for the values
// Wokwi writes: integral values without a decimal point, others in shortest
// round-trip form ("21.01", "-28.8").
std::string formatNumber(double v);

// Rounds to 2 decimals, the precision Wokwi stores positions and wire
// segment lengths with. Applied when the editor creates a value, never on
// save, so loaded values are written back untouched.
double round2(double v);

// Pretty-prints JSON exactly like Wokwi's diagram editor (Prettier style):
// 2-space indent, "{ a, b }" / "[ a, b ]" on one line when the value plus
// any trailing comma fits in `width` columns, otherwise one member per line.
// Wokwi breaks lines of 100 characters and keeps 99, so the default is 99.
// No trailing newline (Wokwi writes none).
std::string prettyPrint(const Json& value, int width = 99);

} // namespace chiply
