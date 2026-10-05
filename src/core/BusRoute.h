#pragma once
// Bus routing (PLAN.md 4.6): several wires drawn together, following a lead
// wire in parallel tracks one grid step apart.
//
// The wires start at pins stacked in a column (or lined up in a row). They
// leave the pins at right angles to that arrangement, each on its own pin's
// line; after every turn of the lead they run in tracks `grid` apart, always
// on the same side of the lead, so a turn towards a wire's side makes it the
// inner, shorter one and a turn away makes it the outer, longer one. Wires
// never cross.
#include "core/WirePath.h"

#include <cstddef>
#include <vector>

namespace chiply {

struct BusOrder {
    bool column = true;             // pins stacked in Y (else lined up in X)
    std::vector<int> rank;          // per pin: tracks from the lead along the axis (+ = larger Y or X)
    std::vector<std::size_t> order; // connection order: the lead, then outward from it
};

// How the pins of a bus are arranged and numbered. Column when they spread
// more in Y than in X. The lead is number 0; the others follow by distance
// from it (the lower coordinate first when two are equally far).
BusOrder busOrder(const std::vector<Point>& pins, std::size_t lead);

// The path of one bus wire, from `start`, for a lead path `lead` (first
// point: the lead's pin). `firstHorizontal`: the wires leave the pins
// horizontally (column) or vertically (row); `firstSign` is the direction
// (+1 right/down, -1 left/up) to assume while the lead has not moved along
// that axis yet. `rank` as in BusOrder.
std::vector<Point> busWirePath(const std::vector<Point>& lead, bool firstHorizontal, int firstSign, Point start,
                               int rank, double grid);

// After the lead is connected (its whole path is `leadFinal`), the other
// wires are connected one at a time. Each keeps its part of the bundle up
// to where it enters the last track (`fixed`), and from there runs along
// that track (`legHorizontal`: the track's axis) to its own pin and turns
// into it. `stub` is `fixed` plus the track up to where the lead left it,
// for showing wires that are still waiting.
struct BusFanOut {
    std::vector<Point> fixed;
    std::vector<Point> stub;
    bool legHorizontal = true;
};
BusFanOut busFanOut(const std::vector<Point>& leadFinal, bool firstHorizontal, int firstSign, Point start, int rank,
                    double grid);

} // namespace chiply
