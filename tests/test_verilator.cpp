// Optional Verilator backend (PLAN.md 6.6): built, cached and loaded at run
// time; every chip net it computes matches the built-in simulator. Skipped
// when Verilator or a C++ compiler is missing.
#include "core/Netlist.h"
#include "core/WokwiJson.h"
#include "sim/Simulator.h"
#include "sim/TruthTable.h"
#include "vl/VerilatorChip.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>

using namespace chiply;
using namespace chiply::sim;

namespace {

std::string readAll(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const std::string kRef = CHIPLY_REFERENCE_DIR;

vl::BuildOptions testBuild()
{
    vl::BuildOptions o;
    o.cacheDir = CHIPLY_VL_TEST_CACHE;
    return o;
}

// The reference design with mux1..9 fixed (as in Ken's copy): no loops.
Document fixedReference()
{
    Document d = loadWokwi(readAll(kRef + "/wokwi_414123795172381697.diagram.json")).doc;
    const Netlist nl = Netlist::build(d, PartLibrary::builtin());
    for (int i = 1; i <= 9; ++i) {
        const std::string mux = "mux" + std::to_string(i);
        std::string flop;
        for (const NetPin& np : nl.nets[size_t(nl.netOf({mux, "OUT"}))].pins)
            if (nl.devices[size_t(np.device)].pinNames[size_t(np.pin)] == "D")
                flop = nl.devices[size_t(np.device)].partId;
        for (Wire& w : d.wires)
            if ((w.from.str() == mux + ":OUT" && w.to.str() == mux + ":A") || (w.to.str() == mux + ":OUT" && w.from.str() == mux + ":A")) {
                w.from = {flop, "Q"};
                w.to = {mux, "A"};
            }
    }
    d.parts.erase(std::remove_if(d.parts.begin(), d.parts.end(), [](const Part& p) { return p.id == "ttio5"; }), d.parts.end());
    return d;
}

} // namespace

TEST_CASE("Verilator chip: truth table of the template, and the build cache", "[verilator]")
{
    std::string why;
    const auto tools = vl::findTools(&why);
    if (!tools)
        SKIP(why);
    const Document d = loadWokwi(readAll(kRef + "/tt_template_354858054593504257.diagram.json")).doc;
    vl::BuildInfo first, second;
    auto chip = vl::buildChip(d, *tools, testBuild(), &first);
    vl::buildChip(d, *tools, testBuild(), &second);
    CHECK(second.fromCache);
    CHECK(second.seconds < 1.0);
    CHECK(chip->name() == "Verilator " + tools->version);

    const Netlist nl = Netlist::build(d, PartLibrary::builtin());
    Options o;
    o.board = false;
    o.chip = chip;
    Simulator sim(nl, o);
    std::vector<int> in, out;
    for (int b = 0; b < 8; ++b) {
        in.push_back(sim.netOf(PinRef{"ttin", "IN" + std::to_string(b)}));
        out.push_back(sim.netOf(PinRef{"ttout", "OUT" + std::to_string(b)}));
    }
    const TruthResult r = runTruthTable(sim, parseTruthTable(readAll(std::string(CHIPLY_TEST_DATA_DIR) + "/tt_template_truthtable.md")), in, out);
    CHECK(r.failures.empty());
    CHECK(r.checked == 7);
}

TEST_CASE("Verilator chip matches the built-in simulator on every chip net", "[verilator]")
{
    std::string why;
    const auto tools = vl::findTools(&why);
    if (!tools)
        SKIP(why);
    const Document d = fixedReference();
    auto chip = vl::buildChip(d, *tools, testBuild());
    const Netlist nl = Netlist::build(d, PartLibrary::builtin());
    Options common;
    common.board = false;
    common.flopStart = FlopStart::Zero; // both engines start every flip-flop at 0
    Options withChip = common;
    withChip.chip = chip;
    Simulator ref(nl, common), vlsim(nl, withChip);

    std::map<int, std::string> uio;
    for (const Part& p : d.parts)
        if (p.type == "board-tt-block-bidirectional-io")
            uio[std::stoi(p.attrs.value("verilogBit", std::string("0")))] = p.id;
    auto drive = [&](Simulator& s, const PinRef& p, V v) { s.drive(p, v); };
    std::mt19937 rng(4242);
    int compared = 0, mismatched = 0;
    std::string first;
    std::set<std::string> distinct;
    for (int cycle = 0; cycle < 1500; ++cycle) {
        const unsigned ui = rng() & 0xff, uioIn = rng() & 0xff;
        const bool rstn = !(cycle < 3 || rng() % 100 == 0);
        for (Simulator* s : {&ref, &vlsim}) {
            for (int b = 0; b < 8; ++b) {
                drive(*s, {"ttin", "IN" + std::to_string(b)}, fromBool((ui >> b) & 1));
                drive(*s, {uio[b], "IN"}, fromBool((uioIn >> b) & 1));
            }
            drive(*s, {"ttin", "RST_N"}, fromBool(rstn));
            drive(*s, {"ttin", "CLK"}, V::L);
            REQUIRE(s->settle());
        }
        for (int phase = 0; phase < 2; ++phase) {
            if (phase == 1)
                for (Simulator* s : {&ref, &vlsim}) {
                    drive(*s, {"ttin", "CLK"}, V::H);
                    REQUIRE(s->settle());
                }
            std::string uo;
            for (int b = 7; b >= 0; --b)
                uo += toChar(ref.value(PinRef{"ttout", "OUT" + std::to_string(b)}));
            distinct.insert(uo);
            for (int net : chip->nets()) {
                ++compared;
                if (ref.value(net) != vlsim.value(net)) {
                    ++mismatched;
                    if (first.empty())
                        first = "cycle " + std::to_string(cycle) + " net " + nl.nets[size_t(net)].name + ": built-in "
                            + toChar(ref.value(net)) + ", Verilator " + toChar(vlsim.value(net));
                }
            }
        }
    }
    INFO(first);
    CHECK(mismatched == 0);
    CHECK(compared > 1000000);
    CHECK(distinct.size() > 20); // outputs really move
}

TEST_CASE("Verilator chip on the board: Tiny Snake runs", "[verilator]")
{
    std::string why;
    const auto tools = vl::findTools(&why);
    if (!tools)
        SKIP(why);
    const Document d = fixedReference();
    const Netlist nl = Netlist::build(d, PartLibrary::builtin());
    Options o;
    o.chip = vl::buildChip(d, *tools, testBuild());
    o.seed = 99;
    Simulator s(nl, o);
    s.setSwitch("sw1", 4, true);  // DIP 5 on
    s.setSwitch("sw2", 0, false); // 10 kHz clock
    std::set<unsigned> shown;
    for (int ms = 0; ms < 2000; ++ms) {
        REQUIRE(s.advance(1'000'000'000));
        shown.insert(*s.segments("sevseg1"));
    }
    CHECK(shown.size() > 3); // the snake moves on the display
}
