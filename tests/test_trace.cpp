// Traces (PLAN.md 6.4): change logging, logic analyzer channels, VCD, and
// Tiny Tapeout truth tables.
#include "core/Netlist.h"
#include "core/WokwiJson.h"
#include "sim/Simulator.h"
#include "sim/Trace.h"
#include "sim/TruthTable.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <memory>
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

struct Design {
    Document doc;
    std::unique_ptr<Netlist> nl;
    std::unique_ptr<Simulator> sim;
    Design(const std::string& path, Options opt = {})
    {
        doc = loadWokwi(readAll(path)).doc;
        nl = std::make_unique<Netlist>(Netlist::build(doc, PartLibrary::builtin()));
        sim = std::make_unique<Simulator>(*nl, opt);
    }
    int net(const char* pin) const { return sim->netOf(*PinRef::parse(pin)); }
};

const std::string kAnalyzer = std::string(CHIPLY_TEST_DATA_DIR) + "/logic_analyzer.diagram.json";
const std::string kTemplate = std::string(CHIPLY_REFERENCE_DIR) + "/tt_template_354858054593504257.diagram.json";

} // namespace

TEST_CASE("logic analyzer channels record the clock and its inverse", "[trace]")
{
    Design d(kAnalyzer);
    Trace t;
    t.addLogicAnalyzers(*d.sim);
    REQUIRE(t.hasAnalyzer());
    CHECK(t.defaultFileName() == "my-trace.vcd");
    REQUIRE(t.channels().size() == 3); // channels = 3
    CHECK(t.channels()[0].scope == "logic1");
    CHECK(t.channels()[0].name == "D0");

    d.sim->advance(2'000'000'000); // 2 ms of a 1 kHz clock
    t.collect(*d.sim);
    const auto& d0 = t.channels()[0].samples;
    const auto& d1 = t.channels()[1].samples;
    REQUIRE(d0.size() == 5); // 0 at t=0, then a change every 0.5 ms
    CHECK(d0[1] == std::pair<Time, V>{500'000'000, V::H});
    CHECK(d0[4] == std::pair<Time, V>{2'000'000'000, V::L});
    for (std::size_t i = 0; i < d0.size(); ++i) {
        CHECK(d1[i].first == d0[i].first);
        CHECK(d1[i].second == vnot(d0[i].second));
    }
    CHECK(t.channels()[0].at(600'000'000) == V::H);
    CHECK(t.channels()[0].at(1'000'000'000) == V::L);
    CHECK(t.channels()[2].at(1'000'000'000) == V::Z);
}

TEST_CASE("VCD output", "[trace]")
{
    Design d(kAnalyzer);
    Trace t;
    t.addLogicAnalyzers(*d.sim);
    t.add(*d.sim, "probes", "not1:OUT", d.net("not1:OUT"));
    d.sim->advance(1'000'000'000);
    t.collect(*d.sim);
    std::ostringstream out;
    t.writeVcd(out, d.sim->now() + 1000);
    const std::string vcd = out.str();
    CHECK(vcd.find("$timescale 1ps $end") != std::string::npos);
    CHECK(vcd.find("$scope module logic1 $end\n$var wire 1 ! D0 $end\n") != std::string::npos);
    CHECK(vcd.find("$scope module probes $end\n$var wire 1 $ not1_OUT $end") != std::string::npos);
    CHECK(vcd.find("#0\n$dumpvars\n0!\n1\"\nz#\n1$\n$end\n") != std::string::npos);
    CHECK(vcd.find("#500000000\n1!\n0\"\n0$\n") != std::string::npos);
    CHECK(vcd.substr(vcd.size() - 12) == "#1000001000\n"); // ends at the requested time
}

TEST_CASE("changes within one time step collapse to the final value", "[trace]")
{
    Design d(kTemplate, [] { Options o; o.board = false; return o; }());
    Trace t;
    const int out0 = d.net("ttout:OUT0");
    t.add(*d.sim, "probes", "out0", out0);
    const V start = d.sim->value(out0);
    d.sim->drive(d.net("ttin:IN0"), V::H);
    d.sim->settle();
    d.sim->drive(d.net("ttin:IN0"), V::L); // back again in the same time step
    d.sim->settle();
    t.collect(*d.sim);
    REQUIRE(t.channels()[0].samples.size() == 1);
    CHECK(t.channels()[0].samples[0].second == start);
    d.sim->drive(d.net("ttin:IN0"), V::H);
    d.sim->advance(1000);
    t.collect(*d.sim);
    CHECK(t.channels()[0].samples.size() == 1); // still time 0: merged
    d.sim->advance(1000);
    d.sim->drive(d.net("ttin:IN0"), V::L);
    d.sim->advance(1000);
    t.collect(*d.sim);
    CHECK(t.channels()[0].samples.size() == 2);
    // A second signal on the same net records independently.
    t.add(*d.sim, "probes", "again", out0);
    d.sim->advance(1000);
    d.sim->drive(d.net("ttin:IN0"), V::H);
    d.sim->advance(1000);
    t.collect(*d.sim);
    CHECK(t.channels()[0].samples.size() == 3);
    CHECK(t.channels()[1].samples.size() == 2);
}

TEST_CASE("truth table rows expand like tt-support-tools", "[truthtable]")
{
    // From tt-support-tools testing/lib/testutils/truthtable.py.
    const TruthTable tt = parseTruthTable("|IN:  CBA  RC  |    output    | comment   |\n"
                                          "|--------------|--------------|-----------|\n"
                                          "| 000 000  00  | -- ----- -   | init      |\n"
                                          "| --- ---  1c  | -- ----- -   | reset     |\n"
                                          "| --- 111  -c  | -- 11100 -   |           |\n"
                                          "| --- 000  tc  | -- ----- -   | reset     |\n");
    CHECK(tt.warnings.empty());
    std::vector<std::string> in, out;
    for (const TruthStep& s : tt.steps) {
        in.push_back(s.in);
        out.push_back(s.out);
    }
    CHECK(in == std::vector<std::string>{
                    "00000000",                                     // init
                    "00000010", "00000011", "00000010", "00000010", // reset: setup, clock up, down, row
                    "00011110", "00011111", "00011110", "00011110", // 111 clocked in
                    "00000000", "00000001", "00000000", "00000010", // t: R pulses low around the clock
                });
    CHECK(out[0] == "--------");
    CHECK(out[1].empty()); // setup and clock steps are not checked
    CHECK(out[8] == "--11100-");
    CHECK(tt.steps[8].line == 5);
    CHECK(tt.steps[8].comment.empty());
    CHECK(tt.steps[12].comment == "reset");
}

TEST_CASE("yosys-style truth tables are read too", "[truthtable]")
{
    const TruthTable tt = parseTruthTable("     \\ui_in | \\uo_out\n"
                                          " ---------- | -----------\n"
                                          " 8'11111000 | 8'xxxxxxx0\n"
                                          " 8'11111001 | 8'xxxxxxx1\n");
    REQUIRE(tt.steps.size() == 2);
    CHECK(tt.steps[1].in == "11111001");
    CHECK(tt.steps[1].out == "-------1");
}

TEST_CASE("unreadable rows are reported", "[truthtable]")
{
    const TruthTable tt = parseTruthTable("| a | b |\n|---|---|\n| 0101 | 1 |\n| 0000 0000 | 01 |\n| # note | |\n");
    CHECK(tt.steps.empty());
    CHECK(tt.warnings.size() == 2);
}

TEST_CASE("truth table on the Tiny Tapeout template", "[truthtable]")
{
    Options o;
    o.board = false;
    Design d(kTemplate, o);
    std::vector<int> in, out;
    for (int b = 0; b < 8; ++b) {
        in.push_back(d.net(("ttin:IN" + std::to_string(b)).c_str()));
        out.push_back(d.net(("ttout:OUT" + std::to_string(b)).c_str()));
    }
    const TruthTable tt = parseTruthTable(readAll(std::string(CHIPLY_TEST_DATA_DIR) + "/tt_template_truthtable.md"));
    TruthResult r = runTruthTable(*d.sim, tt, in, out);
    CHECK(r.failures.empty());
    CHECK(r.checked == 7);
    CHECK(r.steps == 11);

    const TruthTable bad = parseTruthTable("| in | out |\n|--|--|\n| 0000 0001 | 0000 1111 | wrong |\n");
    r = runTruthTable(*d.sim, bad, in, out);
    REQUIRE(r.failures.size() == 1);
    CHECK(r.failures[0] == "line 3: ui_in 00000001 expected uo_out 00001111, got 00001110  (wrong)");
}
