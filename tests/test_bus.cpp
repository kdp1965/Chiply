// Bus routing geometry (PLAN.md 4.6): numbering of the pins, parallel
// tracks one grid apart that stay on their side of the lead, and the
// fan-out after the lead is connected.
#include "core/BusRoute.h"

#include <catch2/catch_test_macros.hpp>

using namespace chiply;

namespace {
constexpr double g = 9.6;
using Pts = std::vector<Point>;
// A column of three pins at x = 0; the lead is the bottom one.
const Pts kColumn = {{0, 0}, {0, 100}, {0, 200}};
} // namespace

TEST_CASE("bus pins are numbered outward from the one clicked", "[bus]")
{
    // Column, bottom pin clicked: it is 0, the next one up is 1, the top 2.
    BusOrder o = busOrder(kColumn, 2);
    CHECK(o.column);
    CHECK(o.rank == std::vector<int>{-2, -1, 0});
    CHECK(o.order == std::vector<std::size_t>{2, 1, 0});
    // Top pin clicked: numbered downwards.
    o = busOrder(kColumn, 0);
    CHECK(o.rank == std::vector<int>{0, 1, 2});
    CHECK(o.order == std::vector<std::size_t>{0, 1, 2});
    // A row (given out of order), rightmost clicked: numbered leftwards.
    o = busOrder({{300, 5}, {100, 0}, {200, 0}}, 0);
    CHECK(!o.column);
    CHECK(o.rank == std::vector<int>{0, -2, -1});
    CHECK(o.order == std::vector<std::size_t>{0, 2, 1});
    // A middle pin: outward, the lower coordinate first.
    o = busOrder(kColumn, 1);
    CHECK(o.order == std::vector<std::size_t>{1, 0, 2});
}

TEST_CASE("bus wires leave their pins side by side and turn in tracks one grid apart", "[bus]")
{
    // Routing right from the column: every wire on its own pin's line.
    Pts lead = {{0, 200}, {300, 200}};
    CHECK(busWirePath(lead, true, 1, kColumn[1], -1, g) == Pts{{0, 100}, {300, 100}});
    CHECK(busWirePath(lead, true, 1, kColumn[0], -2, g) == Pts{{0, 0}, {300, 0}});

    // Right, then down: the top wire is the outer, longest one.
    lead = {{0, 200}, {300, 200}, {300, 400}};
    CHECK(busWirePath(lead, true, 1, kColumn[1], -1, g) == Pts{{0, 100}, {309.6, 100}, {309.6, 400}});
    CHECK(busWirePath(lead, true, 1, kColumn[0], -2, g) == Pts{{0, 0}, {319.2, 0}, {319.2, 400}});
    CHECK(busWirePath(lead, true, 1, kColumn[2], 0, g) == lead); // the lead itself

    // Right, then up: the opposite; the top wire turns first.
    lead = {{0, 200}, {300, 200}, {300, -100}};
    CHECK(busWirePath(lead, true, 1, kColumn[1], -1, g) == Pts{{0, 100}, {290.4, 100}, {290.4, -100}});
    CHECK(busWirePath(lead, true, 1, kColumn[0], -2, g) == Pts{{0, 0}, {280.8, 0}, {280.8, -100}});

    // A second turn keeps every wire on its side: right, down, right again.
    lead = {{0, 200}, {300, 200}, {300, 400}, {500, 400}};
    CHECK(busWirePath(lead, true, 1, kColumn[1], -1, g) == Pts{{0, 100}, {309.6, 100}, {309.6, 390.4}, {500, 390.4}});
    CHECK(busWirePath(lead, true, 1, kColumn[0], -2, g) == Pts{{0, 0}, {319.2, 0}, {319.2, 380.8}, {500, 380.8}});

    // Routing left mirrors it: left, then down, the top wire is again outer.
    lead = {{0, 200}, {-300, 200}, {-300, 400}};
    CHECK(busWirePath(lead, true, 1, kColumn[1], -1, g) == Pts{{0, 100}, {-309.6, 100}, {-309.6, 400}});

    // The lead in the middle: wires above and below keep to their own sides.
    lead = {{0, 100}, {300, 100}, {300, 400}};
    CHECK(busWirePath(lead, true, 1, kColumn[0], -1, g) == Pts{{0, 0}, {309.6, 0}, {309.6, 400}});
    CHECK(busWirePath(lead, true, 1, kColumn[2], 1, g) == Pts{{0, 200}, {290.4, 200}, {290.4, 400}});
}

TEST_CASE("a bus that starts along the pin column still fans out", "[bus]")
{
    // The cursor is straight below the lead: the wires step sideways (in
    // the assumed first direction) so they do not run on top of each other.
    const Pts lead = {{0, 200}, {0, 400}};
    CHECK(busWirePath(lead, true, 1, kColumn[1], -1, g) == Pts{{0, 100}, {9.6, 100}, {9.6, 400}});
    CHECK(busWirePath(lead, true, 1, kColumn[0], -2, g) == Pts{{0, 0}, {19.2, 0}, {19.2, 400}});
    // Nothing drawn yet: each wire is just its pin.
    CHECK(busWirePath({{0, 200}}, true, 1, kColumn[0], -2, g) == Pts{{0, 0}});
}

TEST_CASE("a row of pins leaves vertically", "[bus]")
{
    // Pins in a row at y = 0, lead on the left; down, then right: the
    // rightmost wire is the inner one.
    const Pts lead = {{0, 0}, {0, 200}, {300, 200}};
    CHECK(busWirePath(lead, false, 1, {100, 0}, 1, g) == Pts{{100, 0}, {100, 190.4}, {300, 190.4}});
    CHECK(busWirePath(lead, false, 1, {200, 0}, 2, g) == Pts{{200, 0}, {200, 180.8}, {300, 180.8}});
    // Down, then left: it is the outer one.
    CHECK(busWirePath({{0, 0}, {0, 200}, {-300, 200}}, false, 1, {100, 0}, 1, g)
          == Pts{{100, 0}, {100, 209.6}, {-300, 209.6}});
}

TEST_CASE("after the lead connects, each wire keeps its track and turns into its own pin", "[bus]")
{
    // Right, down a shared track, right into the lead's pin.
    const Pts lead = {{0, 200}, {300, 200}, {300, 500}, {600, 500}};
    BusFanOut f = busFanOut(lead, true, 1, kColumn[1], -1, g);
    CHECK(f.fixed == Pts{{0, 100}, {309.6, 100}}); // up to where it enters the vertical track
    CHECK(!f.legHorizontal);                       // continue down the track first
    CHECK(f.stub == Pts{{0, 100}, {309.6, 100}, {309.6, 500}});
    f = busFanOut(lead, true, 1, kColumn[0], -2, g);
    CHECK(f.fixed == Pts{{0, 0}, {319.2, 0}});

    // The lead went right and straight down into its pin: no shared track
    // beyond the first run, so each wire goes sideways first from its pin.
    f = busFanOut({{0, 200}, {300, 200}, {300, 500}}, true, 1, kColumn[1], -1, g);
    CHECK(f.fixed == Pts{{0, 100}});
    CHECK(f.legHorizontal);
    // Straight across.
    f = busFanOut({{0, 200}, {300, 200}}, true, 1, kColumn[1], -1, g);
    CHECK(f.fixed == Pts{{0, 100}});
    CHECK(f.legHorizontal);
}
