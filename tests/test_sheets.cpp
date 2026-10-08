// Sub-sheets (PLAN.md 7.4): ports, the sheet's block symbol, flattening
// (nested sheets, test parts left out, recursion refused), simulation,
// Verilog export under Icarus and Verilator, and DRC.
#include "core/Drc.h"
#include "core/Netlist.h"
#include "core/Sheets.h"
#include "core/Verilog.h"
#include "core/WokwiJson.h"
#include "sim/Simulator.h"
#include "vl/VerilatorChip.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace chiply;
using namespace chiply::sim;
namespace fs = std::filesystem;

namespace {

const std::string kDemo = std::string(CHIPLY_TEST_DATA_DIR) + "/sheets_demo";

std::string readAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Document demo()
{
    const SheetScan scan = scanSheets({kDemo + "/sheets"});
    REQUIRE(scan.warnings.empty());
    return loadWokwi(readAll(kDemo + "/design.json")).doc;
}

// What the demo design computes: u1 adds two 2-bit numbers with carry in,
// u2 is a separate full adder.
unsigned expected(unsigned v)
{
    const unsigned a = v & 3, b = (v >> 2) & 3, cin = (v >> 4) & 1;
    const unsigned total = a + b + cin; // uo[2:0]
    const unsigned fa = ((v >> 5) & 1) + ((v >> 6) & 1) + ((v >> 7) & 1);
    return total | ((fa & 1) << 3) | ((fa >> 1) << 4);
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

TEST_CASE("a sheet's ports become the pins of its block", "[sheets]")
{
    std::string why;
    const auto s = loadSheet(kDemo + "/sheets/fulladd.json", &why);
    REQUIRE(s);
    CHECK(s->name == "fulladd");
    std::vector<std::string> names;
    for (const SheetPort& p : s->ports)
        names.push_back((p.input ? "in " : "out ") + p.name);
    // Inputs then outputs, each top to bottom as placed in the sheet.
    CHECK(names == std::vector<std::string>{"in a", "in b", "in cin", "out sum", "out cout"});
    const PartDef d = sheetPartDef(*s);
    CHECK(d.type == "chiply-sheet-fulladd");
    CHECK(isExtensionType(d.type));
    CHECK(d.category == "Sheets");
    CHECK(d.findPin("a")->x == 0);
    CHECK(d.findPin("a")->dir == PinDir::In);
    CHECK(d.findPin("sum")->x == d.width);
    CHECK(d.findPin("sum")->dir == PinDir::Out);
    CHECK(d.findPin("a")->y < d.findPin("cin")->y);
    // The port parts themselves.
    const PartDef* in = PartLibrary::builtin().find("chiply-port-in");
    const PartDef* out = PartLibrary::builtin().find("chiply-port-out");
    REQUIRE((in && out));
    CHECK(in->findPin("P")->dir == PinDir::Out); // drives the net inside the sheet
    CHECK(out->findPin("P")->dir == PinDir::In);
    CHECK(isExtensionType(in->type));
}

TEST_CASE("files that are not sheets are reported", "[sheets]")
{
    const fs::path dir = fs::temp_directory_path() / "chiply_bad_sheets";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string empty = R"({"version": 1, "author": "", "editor": "wokwi", "parts": [], "connections": [], "dependencies": {}})";
    std::ofstream(dir / "noports.json") << empty;
    std::ofstream(dir / "2bad.json") << empty;
    std::ofstream(dir / "broken.json") << "{ not json";
    std::ofstream(dir / "good.chiply.json") << "{}"; // a sidecar: skipped silently
    std::ofstream(dir / "good.json") << R"({"version": 1, "author": "", "editor": "wokwi", "parts": [
        {"type": "chiply-port-in", "id": "x", "top": 0, "left": 0, "attrs": {}},
        {"type": "chiply-port-out", "id": "y", "top": 0, "left": 100, "attrs": {}}],
        "connections": [["x:P", "y:P", "green", []]], "dependencies": {}})";
    PartLibrary lib;
    const SheetScan scan = scanSheets({dir.string()}, lib);
    CHECK(scan.loaded == std::vector<std::string>{"good"});
    CHECK(scan.warnings.size() == 3);
    CHECK(lib.find("chiply-sheet-good"));
    fs::remove_all(dir);
}

TEST_CASE("flattening expands instances, nested ones too", "[sheets]")
{
    const Document d = demo();
    REQUIRE(usesSheets(d));
    FlatInfo info;
    const Document flat = flattenSheets(d, PartLibrary::builtin(), &info);
    std::set<std::string> ids;
    for (const Part& p : flat.parts)
        ids.insert(p.id);
    CHECK(ids.count("ttin"));                 // top-level parts keep their ids
    CHECK(ids.count("u2__x1"));               // u2's gates
    CHECK(ids.count("u1__fa0__x1"));          // the full adders inside u1
    CHECK(ids.count("u1__fa1__o1"));
    CHECK(ids.count("u2__a"));                // ports become junctions
    CHECK(flat.findPart("u2__a")->type == "wokwi-junction");
    CHECK(!ids.count("u2"));                  // the instance itself is gone
    CHECK(!ids.count("u2__sw1"));             // test parts are left out
    CHECK(!ids.count("u2__note"));
    CHECK(!ids.count("u2__vcc1"));            // and the VCC that only fed them
    CHECK(!usesSheets(flat));
    // 5 gates per full adder, 3 full adders.
    CHECK(std::count_if(flat.parts.begin(), flat.parts.end(), [](const Part& p) { return p.type.rfind("wokwi-gate-", 0) == 0; }) == 15);
    // Instance pins map to their junctions.
    CHECK(info.resolve({"u1", "a0"}).str() == "u1__a0:J");
    CHECK(info.resolve({"ttin", "IN0"}).str() == "ttin:IN0");
    // A flat design flattens to itself.
    CHECK(saveWokwi(flattenSheets(flat)) == saveWokwi(flat));
}

TEST_CASE("a design with sheets simulates like the flat logic", "[sheets]")
{
    const Document d = demo();
    FlatInfo info;
    const Document flat = flattenSheets(d, PartLibrary::builtin(), &info);
    const Netlist nl = Netlist::build(flat, PartLibrary::builtin());
    Options o;
    o.board = false;
    Simulator sim(nl, o);
    CHECK(sim.warnings().empty());
    int bad = 0;
    for (unsigned v = 0; v < 256; ++v) {
        for (int i = 0; i < 8; ++i)
            sim.drive(PinRef{"ttin", "IN" + std::to_string(i)}, fromBool((v >> i) & 1));
        REQUIRE(sim.settle());
        unsigned got = 0;
        for (int i = 0; i < 5; ++i)
            got |= unsigned(sim.value(PinRef{"ttout", "OUT" + std::to_string(i)}) == V::H) << i;
        bad += got != expected(v);
    }
    CHECK(bad == 0);
    // An instance pin's value through the map (what the canvas shows).
    for (int i = 0; i < 8; ++i)
        sim.drive(PinRef{"ttin", "IN" + std::to_string(i)}, V::H);
    REQUIRE(sim.settle());
    CHECK(sim.value(info.resolve({"u1", "cout"})) == V::H);
    CHECK(sim.value(info.resolve({"u2", "sum"})) == V::H);
}

TEST_CASE("a sheet on its own: ports are driven by the testbench", "[sheets]")
{
    scanSheets({kDemo + "/sheets"});
    const Document d = loadWokwi(readAll(kDemo + "/sheets/fulladd.json")).doc;
    const Netlist nl = Netlist::build(d, PartLibrary::builtin());
    Simulator sim(nl);
    CHECK(sim.warnings().empty()); // ports are not "unsimulated parts"
    CHECK(sim.value(PinRef{"sum", "P"}) == V::L);
    sim.drive(PinRef{"cin", "P"}, V::H);
    REQUIRE(sim.settle());
    CHECK(sim.value(PinRef{"sum", "P"}) == V::H);
    CHECK(sim.value(PinRef{"cout", "P"}) == V::L);
    sim.setSwitch("sw1", 0, true); // the sheet's own test switch: a = 1
    CHECK(sim.value(PinRef{"sum", "P"}) == V::L);
    CHECK(sim.value(PinRef{"cout", "P"}) == V::H);
}

TEST_CASE("a sheet that contains itself is refused", "[sheets]")
{
    const fs::path dir = fs::temp_directory_path() / "chiply_loop_sheets";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "selfy.json") << R"({"version": 1, "author": "", "editor": "wokwi", "parts": [
        {"type": "chiply-port-in", "id": "x", "top": 0, "left": 0, "attrs": {}},
        {"type": "chiply-sheet-other", "id": "inner", "top": 0, "left": 100, "attrs": {}}],
        "connections": [["x:P", "inner:x", "green", []]], "dependencies": {}})";
    std::ofstream(dir / "other.json") << R"({"version": 1, "author": "", "editor": "wokwi", "parts": [
        {"type": "chiply-port-in", "id": "x", "top": 0, "left": 0, "attrs": {}},
        {"type": "chiply-sheet-selfy", "id": "back", "top": 0, "left": 100, "attrs": {}}],
        "connections": [["x:P", "back:x", "green", []]], "dependencies": {}})";
    REQUIRE(scanSheets({dir.string()}).warnings.empty());
    const Document top = loadWokwi(R"({"version": 1, "author": "", "editor": "wokwi", "parts": [
        {"type": "chiply-sheet-selfy", "id": "u1", "top": 0, "left": 0, "attrs": {}},
        {"type": "wokwi-vcc", "id": "v", "top": 0, "left": -50, "attrs": {}}],
        "connections": [["v:VCC", "u1:x", "red", []]], "dependencies": {}})")
                             .doc;
    CHECK_THROWS_AS(flattenSheets(top), FlattenError);
    VerilogOptions o;
    o.moduleName = "tt_um_loop";
    CHECK_THROWS_AS(writeVerilog(top, PartLibrary::builtin(), o), ExportError);
    drc::Engine e;
    e.setExtensionsAllowed(true);
    e.runFull(top);
    REQUIRE(e.violations().size() == 1);
    CHECK(e.violations()[0].check == "sheet-problem");
    CHECK(e.violations()[0].message.find("contains itself") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("the export of a design with sheets is flat and behaves the same", "[sheets]")
{
    const Document d = demo();
    VerilogOptions o;
    o.moduleName = "tt_um_sheets";
    const std::string v = writeVerilog(d, PartLibrary::builtin(), o);
    CHECK(v.find("  xor_cell u1__fa0__x1 (") != std::string::npos);
    CHECK(v.find("  or_cell u2__o1 (") != std::string::npos);
    CHECK(v.find("sw1") == std::string::npos);

    const std::string iverilog = findTool("iverilog"), vvp = findTool("vvp");
    if (iverilog.empty() || vvp.empty())
        SKIP("iverilog/vvp not installed");
    const fs::path dir = fs::temp_directory_path() / "chiply_sheets_icarus";
    fs::create_directories(dir);
    std::ofstream(dir / "dut.v") << v;
    std::ofstream(dir / "cells.v") << ttCellsV();
    {
        std::ofstream tb(dir / "tb.v");
        tb << "`timescale 1ns/1ps\nmodule tb;\nreg [7:0] ui_in; wire [7:0] uo_out, uio_out, uio_oe; integer i;\n"
              "tt_um_sheets dut(.ui_in(ui_in), .uo_out(uo_out), .uio_in(8'h00), .uio_out(uio_out), .uio_oe(uio_oe),"
              " .ena(1'b1), .clk(1'b0), .rst_n(1'b1));\n"
              "initial begin\n  for (i = 0; i < 256; i = i + 1) begin ui_in = i; #1 $display(\"%0d\", uo_out[4:0]); end\n"
              "  $finish;\nend\nendmodule\n";
    }
    fs::remove(dir / "tb.vvp");
    const std::string build = run(iverilog + " -o " + (dir / "tb.vvp").string() + " " + (dir / "tb.v").string() + " "
                                  + (dir / "dut.v").string() + " " + (dir / "cells.v").string() + " 2>&1");
    INFO(build);
    REQUIRE(fs::exists(dir / "tb.vvp"));
    std::istringstream lines(run(vvp + " -n " + (dir / "tb.vvp").string()));
    int bad = 0, n = 0;
    for (unsigned x; lines >> x; ++n)
        bad += x != expected(unsigned(n));
    CHECK(n == 256);
    CHECK(bad == 0);
}

TEST_CASE("sheets through the Verilator engine", "[sheets]")
{
    std::string why;
    const auto tools = vl::findTools(&why);
    if (!tools)
        SKIP(why);
    const Document flat = flattenSheets(demo());
    const Netlist nl = Netlist::build(flat, PartLibrary::builtin());
    vl::BuildOptions bo;
    bo.cacheDir = CHIPLY_VL_TEST_CACHE;
    Options o;
    o.board = false;
    o.chip = vl::buildChip(flat, *tools, bo);
    Simulator sim(nl, o);
    int bad = 0;
    for (unsigned v = 0; v < 256; ++v) {
        for (int i = 0; i < 8; ++i)
            sim.drive(PinRef{"ttin", "IN" + std::to_string(i)}, fromBool((v >> i) & 1));
        REQUIRE(sim.settle());
        unsigned got = 0;
        for (int i = 0; i < 5; ++i)
            got |= unsigned(sim.value(PinRef{"ttout", "OUT" + std::to_string(i)}) == V::H) << i;
        bad += got != expected(v);
    }
    CHECK(bad == 0);
}

TEST_CASE("DRC treats instances as blocks and ports as drivers", "[sheets]")
{
    Document d = demo();
    drc::Engine e; // Wokwi mode: the two instances are extension parts
    e.runFull(d);
    int ext = 0;
    for (const drc::Violation& v : e.violations())
        ext += v.check == "extension-part";
    CHECK(ext == 2);
    e.setExtensionsAllowed(true);
    CHECK(e.violations().empty());
    // An instance input left open.
    d.wires.erase(std::remove_if(d.wires.begin(), d.wires.end(), [](const Wire& w) { return w.to.str() == "u2:b"; }), d.wires.end());
    e.update(d);
    REQUIRE(e.violations().size() == 1);
    CHECK(e.violations()[0].key == "unconnected-input:u2:b");

    // Inside the sheet: inputs fed by a Sheet input count as driven, and an
    // output port with nothing on it is reported.
    Document sheet = loadWokwi(readAll(kDemo + "/sheets/fulladd.json")).doc;
    e.runFull(sheet);
    CHECK(e.violations().empty());
    sheet.wires.erase(std::remove_if(sheet.wires.begin(), sheet.wires.end(), [](const Wire& w) { return w.to.str() == "cout:P"; }),
                      sheet.wires.end());
    e.update(sheet);
    REQUIRE(e.violations().size() == 1);
    CHECK(e.violations()[0].key == "unconnected-input:cout:P");
}
