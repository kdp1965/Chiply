// Design rule checks (PLAN.md 5.2): each check on a small design, the
// reference design's known problems, and incremental == full after edits.
#include "core/Drc.h"
#include "core/WokwiJson.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <random>
#include <set>
#include <sstream>

using namespace chiply;
using namespace chiply::drc;

namespace {

std::string readAll(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Document design(const std::string& parts, const std::string& wires)
{
    return loadWokwi("{\"version\": 1, \"author\": \"t\", \"editor\": \"wokwi\", \"parts\": [" + parts
                     + "], \"connections\": [" + wires + "], \"dependencies\": {}}")
        .doc;
}

std::string part(const std::string& type, const std::string& id, int left = 0, int top = 0,
                 const std::string& attrs = "{}")
{
    return "{\"type\": \"" + type + "\", \"id\": \"" + id + "\", \"top\": " + std::to_string(top)
         + ", \"left\": " + std::to_string(left) + ", \"attrs\": " + attrs + "}";
}

std::string wire(const std::string& a, const std::string& b)
{
    return "[\"" + a + "\", \"" + b + "\", \"green\", []]";
}

std::set<std::string> keys(const Engine& e)
{
    std::set<std::string> k;
    for (const Violation& v : e.violations())
        k.insert(v.key);
    return k;
}

std::set<std::string> fullKeys(const Document& d, const std::set<std::string>& enabledChecks)
{
    Engine e;
    for (const CheckInfo& c : checks())
        e.setEnabled(c.id, enabledChecks.count(c.id) > 0);
    e.runFull(d);
    return keys(e);
}

// Two inverters in a chain, inputs and outputs used: clean.
const std::string kTwoNots = part("wokwi-gate-not", "not1", 0, 0) + "," + part("wokwi-gate-not", "not2", 100, 0) + ","
    + part("wokwi-gate-not", "not3", 200, 0) + "," + part("wokwi-vcc", "vcc1", -50, 0);
const std::string kTwoNotsWires = wire("vcc1:VCC", "not1:IN") + "," + wire("not1:OUT", "not2:IN") + ","
    + wire("not2:OUT", "not3:IN");

} // namespace

TEST_CASE("a clean design has no violations", "[drc]")
{
    Engine e;
    e.runFull(design(kTwoNots, kTwoNotsWires));
    CHECK(e.violations().empty());
    e.setEnabled("unconnected-output", true);
    REQUIRE(e.violations().size() == 1);
    CHECK(e.violations()[0].key == "unconnected-output:not3:OUT");
    CHECK(e.violations()[0].severity == Severity::Info);
}

TEST_CASE("each check on a small design", "[drc]")
{
    Engine e;
    SECTION("unconnected input: no wire, and no driver")
    {
        e.runFull(design(part("wokwi-gate-and-2", "and1") + "," + part("wokwi-gate-not", "not1", 100),
                         wire("and1:B", "not1:IN")));
        CHECK(keys(e) == std::set<std::string>{"unconnected-input:and1:A", "unconnected-input:and1:B",
                                               "unconnected-input:not1:IN"});
        for (const Violation& v : e.violations())
            if (v.key == "unconnected-input:and1:B")
                CHECK(v.message == "and1:B has no driver (net with not1:IN)");
    }
    SECTION("an input driven through a switch is driven")
    {
        e.runFull(design(part("wokwi-gate-not", "not1") + "," + part("wokwi-slide-switch", "sw1", -60) + ","
                             + part("wokwi-vcc", "vcc1", -90),
                         wire("sw1:2", "not1:IN") + "," + wire("vcc1:VCC", "sw1:1")));
        CHECK(keys(e).empty());
    }
    SECTION("multiple drivers and short circuit")
    {
        e.runFull(design(kTwoNots + "," + part("wokwi-gnd", "gnd1", 0, 80) + "," + part("wokwi-gate-not", "not4", 0, 160),
                         kTwoNotsWires + "," + wire("gnd1:GND", "not1:IN") + "," + wire("not4:OUT", "not2:IN") + ","
                             + wire("vcc1:VCC", "not4:IN")));
        CHECK(keys(e) == std::set<std::string>{"short-circuit:gnd1:GND,vcc1:VCC",
                                               "multiple-drivers:not1:OUT,not4:OUT"});
    }
    SECTION("several VCC symbols on one net are fine")
    {
        e.runFull(design(kTwoNots + "," + part("wokwi-vcc", "vcc2", -50, 50), kTwoNotsWires + "," + wire("vcc2:VCC", "not1:IN")));
        CHECK(keys(e).empty());
    }
    SECTION("clock from logic")
    {
        e.runFull(design(kTwoNots + "," + part("wokwi-flip-flop-d", "flop1", 300), kTwoNotsWires + "," + wire("not3:OUT", "flop1:CLK")
                             + "," + wire("vcc1:VCC", "flop1:D")));
        CHECK(keys(e) == std::set<std::string>{"clock-from-logic:flop1"});
        CHECK(e.violations()[0].message == "flop1:CLK is driven by not3:OUT (NOT gate (inverter))");
        CHECK(e.violations()[0].severity == Severity::Warning);
    }
    SECTION("combinational loops: self, and through several cells; a flip-flop breaks them")
    {
        e.runFull(design(part("wokwi-gate-and-2", "and1") + "," + part("wokwi-gate-not", "not1", 100) + ","
                             + part("wokwi-gate-not", "not2", 200) + "," + part("wokwi-vcc", "vcc1", -50) + ","
                             + part("wokwi-mux-2", "mux1", 0, 100) + "," + part("wokwi-flip-flop-d", "flop1", 300) + ","
                             + part("wokwi-gate-not", "not3", 400),
                         wire("vcc1:VCC", "and1:A") + "," + wire("and1:OUT", "not1:IN") + "," + wire("not1:OUT", "not2:IN")
                             + "," + wire("not2:OUT", "and1:B") + "," + wire("mux1:OUT", "mux1:A") + ","
                             + wire("vcc1:VCC", "mux1:B") + "," + wire("vcc1:VCC", "mux1:SEL") + ","
                             + wire("vcc1:VCC", "flop1:CLK") + "," + wire("flop1:Q", "not3:IN") + ","
                             + wire("not3:OUT", "flop1:D")));
        CHECK(keys(e) == std::set<std::string>{"combinational-loop:and1,not1,not2", "combinational-loop:mux1"});
    }
    SECTION("dangling wire and unknown part")
    {
        e.runFull(design(kTwoNots + "," + part("wokwi-frobnicator", "frob1", 0, 90),
                         kTwoNotsWires + "," + wire("not3:OUT", "ghost1:A") + "," + wire("not3:OUT", "not1:NOPE")));
        CHECK(keys(e) == std::set<std::string>{"dangling-wire:not3:OUT|ghost1:A|ghost1:A",
                                               "dangling-wire:not3:OUT|not1:NOPE|not1:NOPE", "unknown-part:frob1"});
    }
    SECTION("invalid ids")
    {
        CHECK(isValidVerilogId("flop30"));
        CHECK(isValidVerilogId("state_reg_0"));
        CHECK(!isValidVerilogId("module"));
        CHECK(!isValidVerilogId("net12"));
        CHECK(!isValidVerilogId("clk"));
        CHECK(!isValidVerilogId("2bad"));
        CHECK(!isValidVerilogId("has space"));
        e.runFull(design(part("wokwi-gate-not", "wire") + "," + part("wokwi-gate-not", "n1", 100) + ","
                             + part("wokwi-gate-not", "n1", 200) + "," + part("wokwi-text", "net5", 0, 50, "{\"text\": \"x\"}"),
                         ""));
        e.setEnabled("unconnected-input", false);
        CHECK(keys(e) == std::set<std::string>{"invalid-id:wire", "invalid-id-duplicate:n1"});
    }
    SECTION("Tiny Tapeout bidirectional bits")
    {
        e.runFull(design(part("board-tt-block-bidirectional-io", "io1", 0, 0, "{\"verilogBit\": \"3\"}") + ","
                             + part("board-tt-block-bidirectional-io", "io2", 0, 100, "{\"verilogBit\": \"3\"}") + ","
                             + part("board-tt-block-bidirectional-io", "io3", 0, 200, "{}") + ","
                             + part("board-tt-block-bidirectional-io", "io4", 0, 300, "{\"verilogBit\": \"9\"}"),
                         ""));
        e.setEnabled("unconnected-input", false);
        CHECK(keys(e) == std::set<std::string>{"tt-bidir-bit:bit3", "tt-bidir-bit:io3", "tt-bidir-bit:io4"});
    }
}

TEST_CASE("the reference design's known problems", "[drc]")
{
    const Document d = loadWokwi(readAll(std::string(CHIPLY_REFERENCE_DIR) + "/wokwi_414123795172381697.diagram.json")).doc;
    Engine e;
    const Stats& st = e.runFull(d);
    CHECK(st.full);
    std::set<std::string> want = {"tt-bidir-bit:bit5", "stacked-parts:ttio5,ttio8", "unconnected-input:ttio5:OE",
                                  "unconnected-input:ttio5:OUT"};
    for (int i = 1; i <= 9; ++i)
        want.insert("combinational-loop:mux" + std::to_string(i));
    CHECK(keys(e) == want);
    CHECK(e.count(Severity::Error) == 10);
    CHECK(e.count(Severity::Warning) == 3);

    const Document t = loadWokwi(readAll(std::string(CHIPLY_REFERENCE_DIR) + "/tt_template_354858054593504257.diagram.json")).doc;
    e.runFull(t);
    CHECK(e.violations().empty());
}

TEST_CASE("incremental checking re-checks only what an edit touched", "[drc]")
{
    Document d = loadWokwi(readAll(std::string(CHIPLY_REFERENCE_DIR) + "/wokwi_414123795172381697.diagram.json")).doc;
    Engine e;
    e.update(d); // first call: full
    REQUIRE(e.lastStats().full);

    SECTION("a route or colour change checks nothing")
    {
        d.wires[10].color = "red";
        d.wires[10].path = *parseWirePath({"v10", "h10"});
        const Stats& st = e.update(d);
        CHECK(!st.full);
        CHECK(st.partsChecked == 0);
    }
    SECTION("fixing mux1's loop clears just that violation")
    {
        // mux1:OUT -> mux1:A becomes flop1:Q -> mux1:A (the fix in the copy).
        bool found = false;
        for (Wire& w : d.wires)
            if ((w.from.str() == "mux1:OUT" && w.to.str() == "mux1:A") || (w.from.str() == "mux1:A" && w.to.str() == "mux1:OUT")) {
                w.from = *PinRef::parse("flop1:Q");
                w.to = *PinRef::parse("mux1:A");
                found = true;
            }
        REQUIRE(found);
        const Stats& st = e.update(d);
        CHECK(!st.full);
        CHECK(st.partsChecked > 0);
        // flop1:Q fans out widely: every part on the old and new nets of the
        // touched parts is re-checked, still far fewer than a full pass (980).
        CHECK(st.partsChecked < 120);
        CHECK(!keys(e).count("combinational-loop:mux1"));
        CHECK(keys(e).count("combinational-loop:mux2"));
        CHECK(keys(e) == fullKeys(d, e.enabledChecks()));
    }
}

TEST_CASE("incremental results always equal a full pass", "[drc]")
{
    Document d = loadWokwi(readAll(std::string(CHIPLY_REFERENCE_DIR) + "/wokwi_414123795172381697.diagram.json")).doc;
    Engine inc;
    inc.setEnabled("unconnected-output", true);
    inc.update(d);
    std::mt19937 rng(1234);
    auto pick = [&](std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng); };
    for (int step = 0; step < 300; ++step) {
        const int kind = int(pick(7));
        if (kind == 0 && d.parts.size() > 10) { // delete a part (its wires dangle)
            d.parts.erase(d.parts.begin() + std::ptrdiff_t(pick(d.parts.size())));
        } else if (kind == 1 && !d.wires.empty()) { // delete a wire
            d.wires.erase(d.wires.begin() + std::ptrdiff_t(pick(d.wires.size())));
        } else if (kind == 2) { // wire two random pins together
            const Part& a = d.parts[pick(d.parts.size())];
            const Part& b = d.parts[pick(d.parts.size())];
            const PartDef* da = PartLibrary::builtin().find(a.type);
            const PartDef* db = PartLibrary::builtin().find(b.type);
            if (da && db && !da->pins.empty() && !db->pins.empty()) {
                Wire w;
                w.from = {a.id, da->pins[pick(da->pins.size())].name};
                w.to = {b.id, db->pins[pick(db->pins.size())].name};
                d.wires.push_back(w);
            }
        } else if (kind == 3) { // move a part onto another of the same type, or away
            Part& a = d.parts[pick(d.parts.size())];
            const Part& b = d.parts[pick(d.parts.size())];
            if (a.type == b.type) {
                a.left = b.left;
                a.top = b.top;
            } else {
                a.left += 9.6;
            }
        } else if (kind == 4) { // rename a part (its wires keep the old name: they dangle)
            Part& a = d.parts[pick(d.parts.size())];
            a.id = pick(4) == 0 ? "module" : a.id + "x";
        } else if (kind == 5) { // add a gate wired to something
            Part p;
            p.type = pick(2) ? "wokwi-gate-not" : "wokwi-vcc";
            p.id = "new" + std::to_string(step);
            d.parts.push_back(p);
            const Part& b = d.parts[pick(d.parts.size())];
            if (const PartDef* db = PartLibrary::builtin().find(b.type); db && !db->pins.empty()) {
                Wire w;
                w.from = {p.id, p.type == "wokwi-vcc" ? "VCC" : "OUT"};
                w.to = {b.id, db->pins[pick(db->pins.size())].name};
                d.wires.push_back(w);
            }
        } else { // change a bidirectional block's bit
            for (Part& p : d.parts)
                if (p.type == "board-tt-block-bidirectional-io" && pick(3) == 0)
                    p.attrs["verilogBit"] = std::to_string(pick(9));
        }
        inc.update(d);
        const auto a = keys(inc), b = fullKeys(d, inc.enabledChecks());
        std::string diff;
        for (const auto& k : a)
            if (!b.count(k))
                diff += " +" + k; // only incremental has it
        for (const auto& k : b)
            if (!a.count(k))
                diff += " -" + k; // only the full pass has it
        INFO("step " << step << " kind " << kind << " last wire " << d.wires.back().from.str() << " - "
                      << d.wires.back().to.str() << ":" << diff);
        REQUIRE(diff.empty());
    }
}

TEST_CASE("turning checks on and off", "[drc]")
{
    Engine e;
    e.setEnabled("unconnected-input", false);
    e.update(design(part("wokwi-gate-not", "not1"), ""));
    CHECK(e.violations().empty());
    e.setEnabled("unconnected-input", true); // re-checks the design for it
    CHECK(keys(e) == std::set<std::string>{"unconnected-input:not1:IN"});
    e.setEnabled("unconnected-input", false);
    CHECK(e.violations().empty());
    CHECK(findCheck("multiple-drivers")->severity == Severity::Error);
    CHECK(!findCheck("nope"));
}
