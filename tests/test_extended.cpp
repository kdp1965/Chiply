// Chiply extended cells (PLAN.md 7.1): every cell's truth table in the
// built-in simulator, the same through Chiply's Verilog export run by Icarus
// (cells.v + chiply_cells.v) and through the Verilator engine, DRC in Wokwi
// and Extended mode, and the Tiny Tapeout export adding chiply_cells.v.
#include "core/Drc.h"
#include "core/Netlist.h"
#include "core/Verilog.h"
#include "core/WokwiJson.h"
#include "sim/Simulator.h"
#include "vl/VerilatorChip.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

using namespace chiply;
using namespace chiply::sim;
namespace fs = std::filesystem;

namespace {

struct Cell {
    const char* type;
    std::function<bool(unsigned)> f; // inputs in pin order: bit i = input i
};

bool b(unsigned v, int i) { return (v >> i) & 1; }

const std::vector<Cell> kCells = {
    {"chiply-gate-and-3", [](unsigned v) { return b(v, 0) && b(v, 1) && b(v, 2); }},
    {"chiply-gate-and-4", [](unsigned v) { return b(v, 0) && b(v, 1) && b(v, 2) && b(v, 3); }},
    {"chiply-gate-nand-3", [](unsigned v) { return !(b(v, 0) && b(v, 1) && b(v, 2)); }},
    {"chiply-gate-nand-4", [](unsigned v) { return !(b(v, 0) && b(v, 1) && b(v, 2) && b(v, 3)); }},
    {"chiply-gate-or-3", [](unsigned v) { return b(v, 0) || b(v, 1) || b(v, 2); }},
    {"chiply-gate-or-4", [](unsigned v) { return b(v, 0) || b(v, 1) || b(v, 2) || b(v, 3); }},
    {"chiply-gate-nor-3", [](unsigned v) { return !(b(v, 0) || b(v, 1) || b(v, 2)); }},
    {"chiply-gate-nor-4", [](unsigned v) { return !(b(v, 0) || b(v, 1) || b(v, 2) || b(v, 3)); }},
    {"chiply-gate-xor-3", [](unsigned v) { return b(v, 0) ^ b(v, 1) ^ b(v, 2); }},
    {"chiply-maj-3", [](unsigned v) { return int(b(v, 0)) + int(b(v, 1)) + int(b(v, 2)) >= 2; }},
    {"chiply-mux-4", [](unsigned v) { return b(v, 5) ? (b(v, 4) ? b(v, 3) : b(v, 2)) : (b(v, 4) ? b(v, 1) : b(v, 0)); }},
    {"chiply-a21oi", [](unsigned v) { return !((b(v, 0) && b(v, 1)) || b(v, 2)); }},
    {"chiply-a21o", [](unsigned v) { return (b(v, 0) && b(v, 1)) || b(v, 2); }},
    {"chiply-o21ai", [](unsigned v) { return !((b(v, 0) || b(v, 1)) && b(v, 2)); }},
    {"chiply-o21a", [](unsigned v) { return (b(v, 0) || b(v, 1)) && b(v, 2); }},
    {"chiply-a22oi", [](unsigned v) { return !((b(v, 0) && b(v, 1)) || (b(v, 2) && b(v, 3))); }},
    {"chiply-o22ai", [](unsigned v) { return !((b(v, 0) || b(v, 1)) && (b(v, 2) || b(v, 3))); }},
};

std::vector<std::string> inputPins(const char* type)
{
    std::vector<std::string> pins;
    for (const PinDef& p : PartLibrary::builtin().find(type)->pins)
        if (p.dir == PinDir::In)
            pins.push_back(p.name);
    return pins;
}

// A Tiny Tapeout design: cells k0.. k0+7 read ui_in[0..5] (inputs in pin
// order) and drive uo_out[0..7].
Document group(std::size_t k0)
{
    std::string parts = R"({"type": "board-tt-block-input", "id": "ttin", "top": 0, "left": -200, "attrs": {}},)"
                        R"({"type": "board-tt-block-output", "id": "ttout", "top": 0, "left": 400, "attrs": {}})";
    std::string wires;
    for (std::size_t i = 0; i < 8 && k0 + i < kCells.size(); ++i) {
        const std::string id = "c" + std::to_string(i);
        parts += R"(,{"type": ")" + std::string(kCells[k0 + i].type) + R"(", "id": ")" + id + R"(", "top": )"
            + std::to_string(i * 100) + R"(, "left": 0, "attrs": {}})";
        const auto pins = inputPins(kCells[k0 + i].type);
        for (std::size_t p = 0; p < pins.size(); ++p)
            wires += R"(["ttin:IN)" + std::to_string(p) + R"(", ")" + id + ":" + pins[p] + R"(", "green", []],)";
        wires += R"([")" + id + R"(:OUT", "ttout:OUT)" + std::to_string(i) + R"(", "green", []],)";
    }
    wires.pop_back();
    return loadWokwi(R"({"version": 1, "author": "", "editor": "wokwi", "parts": [)" + parts + R"(], "connections": [)"
                     + wires + R"(], "dependencies": {}})")
        .doc;
}

// uo_out for every ui_in 0..63 in a simulator (chip-only).
std::vector<unsigned> sweep(Simulator& sim)
{
    std::vector<unsigned> out;
    for (unsigned v = 0; v < 64; ++v) {
        for (int i = 0; i < 8; ++i)
            sim.drive(PinRef{"ttin", "IN" + std::to_string(i)}, fromBool((v >> i) & 1));
        REQUIRE(sim.settle());
        unsigned o = 0;
        for (int i = 0; i < 8; ++i)
            o |= unsigned(sim.value(PinRef{"ttout", "OUT" + std::to_string(i)}) == V::H) << i;
        out.push_back(o);
    }
    return out;
}

std::vector<unsigned> expected(std::size_t k0)
{
    std::vector<unsigned> out;
    for (unsigned v = 0; v < 64; ++v) {
        unsigned o = 0;
        for (std::size_t i = 0; i < 8 && k0 + i < kCells.size(); ++i)
            o |= unsigned(kCells[k0 + i].f(v)) << i;
        out.push_back(o);
    }
    return out;
}

std::string findTool(const char* name)
{
#ifdef __EMSCRIPTEN__
    (void)name;
    return {}; // no processes in WebAssembly
#endif
    for (const char* dir : {"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"})
        if (fs::exists(fs::path(dir) / name))
            return (fs::path(dir) / name).string();
    return {};
}

std::string run(const std::string& cmd)
{
    std::string out;
    if (FILE* f = popen(cmd.c_str(), "r")) {
        char buf[4096];
        while (std::size_t n = fread(buf, 1, sizeof buf, f))
            out.append(buf, n);
        pclose(f);
    }
    return out;
}

} // namespace

TEST_CASE("every extended cell is in the library, with a cell in chiply_cells.v", "[extended]")
{
    for (const Cell& c : kCells) {
        const PartDef* d = PartLibrary::builtin().find(c.type);
        REQUIRE(d);
        CHECK(isExtensionType(d->type));
        CHECK(drc::isCombinational(d));
        const std::string cell = d->verilog["cell"].get<std::string>();
        CHECK(std::string(chiplyCellsV()).find("module " + cell + " (") != std::string::npos);
    }
}

TEST_CASE("extended cells in the built-in simulator: full truth tables", "[extended]")
{
    for (std::size_t k0 = 0; k0 < kCells.size(); k0 += 8) {
        const Document d = group(k0);
        const Netlist nl = Netlist::build(d, PartLibrary::builtin());
        Options o;
        o.board = false;
        Simulator sim(nl, o);
        CHECK(sweep(sim) == expected(k0));
    }
    // Four-state: an unknown select with equal data stays known.
    const Document d = group(8); // c2 is the 4-input mux
    const Netlist nl = Netlist::build(d, PartLibrary::builtin());
    Options o;
    o.board = false;
    o.wokwiLogic = false;
    Simulator sim(nl, o);
    for (int i = 0; i < 4; ++i)
        sim.drive(PinRef{"ttin", "IN" + std::to_string(i)}, V::H);
    sim.drive(PinRef{"ttin", "IN4"}, V::X);
    sim.drive(PinRef{"ttin", "IN5"}, V::X);
    REQUIRE(sim.settle());
    CHECK(sim.value(PinRef{"c2", "OUT"}) == V::H);
    sim.drive(PinRef{"ttin", "IN0"}, V::L);
    REQUIRE(sim.settle());
    CHECK(sim.value(PinRef{"c2", "OUT"}) == V::X);
}

TEST_CASE("extended cells exported to Verilog behave the same under Icarus", "[extended]")
{
    const std::string iverilog = findTool("iverilog"), vvp = findTool("vvp");
    if (iverilog.empty() || vvp.empty())
        SKIP("iverilog/vvp not installed");
    const fs::path dir = fs::temp_directory_path() / "chiply_extended";
    fs::create_directories(dir);
    {
        std::ofstream(dir / "cells.v") << ttCellsV();
        std::ofstream(dir / "chiply_cells.v") << chiplyCellsV();
    }
    for (std::size_t k0 = 0; k0 < kCells.size(); k0 += 8) {
        const Document d = group(k0);
        REQUIRE(usesChiplyCells(d));
        VerilogOptions vo;
        vo.moduleName = "tt_um_ext";
        {
            std::ofstream(dir / "dut.v") << writeVerilog(d, PartLibrary::builtin(), vo);
            std::ofstream tb(dir / "tb.v");
            tb << "`timescale 1ns/1ps\nmodule tb;\nreg [7:0] ui_in; wire [7:0] uo_out, uio_out, uio_oe; integer i;\n"
                  "tt_um_ext dut(.ui_in(ui_in), .uo_out(uo_out), .uio_in(8'h00), .uio_out(uio_out), .uio_oe(uio_oe),"
                  " .ena(1'b1), .clk(1'b0), .rst_n(1'b1));\n"
                  "initial begin\n  for (i = 0; i < 64; i = i + 1) begin ui_in = i; #1 $display(\"%0d\", uo_out); end\n"
                  "  $finish;\nend\nendmodule\n";
        }
        fs::remove(dir / "tb.vvp");
        const std::string build = run(iverilog + " -o " + (dir / "tb.vvp").string() + " " + (dir / "tb.v").string() + " "
                                      + (dir / "dut.v").string() + " " + (dir / "cells.v").string() + " "
                                      + (dir / "chiply_cells.v").string() + " 2>&1");
        INFO(build);
        REQUIRE(fs::exists(dir / "tb.vvp"));
        std::istringstream lines(run(vvp + " -n " + (dir / "tb.vvp").string()));
        std::vector<unsigned> got;
        for (unsigned x; lines >> x;)
            got.push_back(x);
        CHECK(got == expected(k0));
    }
}

TEST_CASE("extended cells through the Verilator engine", "[extended]")
{
    std::string why;
    const auto tools = vl::findTools(&why);
    if (!tools)
        SKIP(why);
    vl::BuildOptions bo;
    bo.cacheDir = CHIPLY_VL_TEST_CACHE;
    for (std::size_t k0 = 0; k0 < kCells.size(); k0 += 8) {
        const Document d = group(k0);
        const Netlist nl = Netlist::build(d, PartLibrary::builtin());
        Options o;
        o.board = false;
        o.chip = vl::buildChip(d, *tools, bo);
        Simulator sim(nl, o);
        CHECK(sweep(sim) == expected(k0));
    }
}

TEST_CASE("DRC: extension parts in Wokwi mode, cells as combinational logic", "[extended]")
{
    const Document d = group(0);
    drc::Engine e; // Wokwi mode by default
    e.runFull(d);
    int ext = 0;
    for (const drc::Violation& v : e.violations())
        ext += v.check == "extension-part";
    CHECK(ext == 8);
    e.setExtensionsAllowed(true);
    for (const drc::Violation& v : e.violations())
        CHECK(v.check != "extension-part");

    // A loop through an AOI cell, and a flop clocked by a 3-input AND.
    const Document loop = loadWokwi(R"({"version": 1, "author": "", "editor": "wokwi", "parts": [
        {"type": "chiply-a21oi", "id": "aoi1", "top": 0, "left": 0, "attrs": {}},
        {"type": "chiply-gate-and-3", "id": "and1", "top": 100, "left": 0, "attrs": {}},
        {"type": "wokwi-flip-flop-d", "id": "flop1", "top": 200, "left": 0, "attrs": {}},
        {"type": "wokwi-vcc", "id": "vcc1", "top": 0, "left": -50, "attrs": {}}],
      "connections": [["aoi1:OUT", "aoi1:A1", "green", []], ["vcc1:VCC", "aoi1:A2", "red", []],
        ["vcc1:VCC", "aoi1:B1", "red", []], ["vcc1:VCC", "and1:A", "red", []], ["vcc1:VCC", "and1:B", "red", []],
        ["vcc1:VCC", "and1:C", "red", []], ["and1:OUT", "flop1:CLK", "green", []], ["vcc1:VCC", "flop1:D", "red", []]],
      "dependencies": {}})")
                              .doc;
    e.runFull(loop);
    std::set<std::string> keys;
    for (const drc::Violation& v : e.violations())
        keys.insert(v.key);
    CHECK(keys.count("combinational-loop:aoi1"));
    CHECK(keys.count("clock-from-logic:flop1"));
}

TEST_CASE("Tiny Tapeout export adds chiply_cells.v when it is needed", "[extended]")
{
    const fs::path dir = fs::temp_directory_path() / "chiply_ext_project";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "info.yaml") << "project:\n  language: \"Wokwi\"\n";
    VerilogOptions vo;
    vo.moduleName = "tt_um_ext";
    exportTtProject(group(0), PartLibrary::builtin(), dir.string(), vo);
    CHECK(fs::exists(dir / "src" / "chiply_cells.v"));
    std::ifstream y(dir / "info.yaml");
    std::stringstream ss;
    ss << y.rdbuf();
    CHECK(ss.str().find("    - \"chiply_cells.v\"\n") != std::string::npos);
    fs::remove_all(dir);
}
