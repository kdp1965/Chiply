#include "core/Geometry.h"
#include "core/PartLibrary.h"
#include "core/WokwiJson.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

using namespace chiply;
using Catch::Approx;

namespace {

Document loadRef(const std::string& name)
{
    std::ifstream in(std::string(CHIPLY_REFERENCE_DIR) + "/" + name, std::ios::binary);
    REQUIRE(in);
    std::ostringstream ss;
    ss << in.rdbuf();
    return loadWokwi(ss.str()).doc;
}

} // namespace

TEST_CASE("builtin library loads and is well formed")
{
    const PartLibrary& lib = PartLibrary::builtin();
    CHECK(lib.parts().size() >= 30);
    for (const PartDef& d : lib.parts()) {
        INFO(d.type);
        CHECK_FALSE(d.symbol.empty());
        CHECK_FALSE(d.prefix.empty());
        std::set<std::string> names;
        for (const PinDef& p : d.pins) {
            CHECK(names.insert(p.name).second);
            if (d.type != "wokwi-text") {
                // Pins sit on the outline edge; Wokwi's DR flip-flop puts R
                // 0.16 mm below its outline, so allow a little slack.
                CHECK(p.x >= -1.0);
                CHECK(p.y >= -1.0);
                CHECK(p.x <= d.width + 1.0);
                CHECK(p.y <= d.height + 1.0);
            }
        }
        if (d.verilog.contains("ports"))
            for (const PinDef& p : d.pins)
                CHECK(d.verilog["ports"].contains(p.name));
    }
}

TEST_CASE("logic part geometry matches Wokwi")
{
    const PartLibrary& lib = PartLibrary::builtin();
    const PartDef* andGate = lib.find("wokwi-gate-and-2");
    REQUIRE(andGate);
    CHECK(andGate->width == Approx(96.0));
    CHECK(andGate->height == Approx(10 * PartLibrary::kPxPerMm));
    CHECK(andGate->findPin("OUT")->x == Approx(96.0));
    CHECK(andGate->findPin("A")->y == Approx(9.6));
    CHECK(andGate->findPin("B")->y == Approx(28.8));
    const PartDef* dsr = lib.find("wokwi-flip-flop-dsr");
    REQUIRE(dsr);
    CHECK(dsr->findPin("CLK")->clock);
    CHECK(dsr->findPin("S")->y == Approx(0.0));
    CHECK(dsr->findPin("R")->y == Approx(57.6));
}

TEST_CASE("pin directions")
{
    const PartLibrary& lib = PartLibrary::builtin();
    CHECK(lib.find("wokwi-gate-and-2")->findPin("OUT")->dir == PinDir::Out);
    CHECK(lib.find("wokwi-mux-2")->findPin("SEL")->dir == PinDir::In);
    CHECK(lib.find("board-tt-block-input")->findPin("IN3")->dir == PinDir::Out);
    CHECK(lib.find("board-tt-block-input")->findPin("EXTIN3")->dir == PinDir::In);
    CHECK(lib.find("board-tt-block-output")->findPin("OUT3")->dir == PinDir::In);
    CHECK(lib.find("board-tt-block-bidirectional-io")->findPin("IN")->dir == PinDir::Out);
    CHECK(lib.find("wokwi-gnd")->findPin("GND")->dir == PinDir::Power);
    CHECK(lib.find("wokwi-pushbutton")->findPin("1.l")->dir == PinDir::Passive);
}

TEST_CASE("rotation is about the outline center, clockwise")
{
    const PartDef* g = PartLibrary::builtin().find("wokwi-gate-and-2");
    Part p;
    p.type = g->type;
    p.left = 100;
    p.top = 200;
    // OUT is at (96, 19.2) locally; the outline center is (48, 18.8976),
    // so OUT sits 0.3024 px below the center line.
    const double cy = 5 * PartLibrary::kPxPerMm, off = 19.2 - cy;
    auto out0 = pinPosition(p, *g, "OUT");
    CHECK(out0->x == Approx(196));
    CHECK(out0->y == Approx(219.2));
    p.rotate = 90; // output now points down
    auto out90 = pinPosition(p, *g, "OUT");
    CHECK(out90->x == Approx(100 + 48 - off));
    CHECK(out90->y == Approx(200 + cy + 48));
    p.rotate = 180;
    CHECK(pinPosition(p, *g, "OUT")->x == Approx(100));
    CHECK(pinPosition(p, *g, "OUT")->y == Approx(200 + cy - off));
    p.rotate = 270;
    CHECK(pinPosition(p, *g, "OUT")->y == Approx(200 + cy - 48));
    Rect b = partBounds(p, *g);
    CHECK(b.w == Approx(g->height));
    CHECK(b.h == Approx(g->width));
}

TEST_CASE("every part and pin in the reference diagrams is known")
{
    const PartLibrary& lib = PartLibrary::builtin();
    for (const char* name : {"wokwi_414123795172381697.diagram.json", "tt_template_354858054593504257.diagram.json"}) {
        INFO(name);
        Document d = loadRef(name);
        for (const Part& p : d.parts) {
            INFO(p.type);
            CHECK(lib.find(p.type));
        }
        for (const Wire& w : d.wires) {
            INFO(w.from.str() << " -> " << w.to.str());
            CHECK(pinPosition(d, lib, w.from));
            CHECK(pinPosition(d, lib, w.to));
        }
    }
}

TEST_CASE("reference wiring is consistent with the calibrated pins")
{
    // Wokwi records the bends a user clicked; whatever is left between the
    // last bend and the target pin is a whole number of half-grid steps when
    // the pin positions are right. A wrong pin offset or rotation pivot shows
    // up immediately as off-grid leftovers.
    const PartLibrary& lib = PartLibrary::builtin();
    Document d = loadRef("wokwi_414123795172381697.diagram.json");
    int checked = 0, onGrid = 0;
    for (const Wire& w : d.wires) {
        auto a = pinPosition(d, lib, w.from);
        auto b = pinPosition(d, lib, w.to);
        REQUIRE(a);
        REQUIRE(b);
        Point end = sourceRouteEnd(*a, w.path);
        for (double gap : {b->x - end.x, b->y - end.y}) {
            if (std::fabs(gap) < 0.6)
                continue;
            ++checked;
            double steps = std::fabs(gap) / 4.8;
            if (std::fabs(steps - std::round(steps)) < 0.03)
                ++onGrid;
        }
    }
    INFO(onGrid << " of " << checked << " leftover gaps on the 4.8 px grid");
    CHECK(onGrid >= checked * 0.9);
}
