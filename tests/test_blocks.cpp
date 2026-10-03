// Custom blocks (PLAN.md 7.2): loading block.json, the auto symbol's pins,
// where blocks are found, the Verilog export (vectors, parameters) run by
// Icarus and by the Verilator engine, DRC and the Tiny Tapeout export.
#include "core/Blocks.h"
#include "core/Drc.h"
#include "core/Netlist.h"
#include "core/Verilog.h"
#include "core/WokwiJson.h"
#include "sim/Simulator.h"
#include "vl/VerilatorChip.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace chiply;
namespace fs = std::filesystem;

namespace {

const std::string kDemo = std::string(CHIPLY_TEST_DATA_DIR) + "/blocks_demo";

std::string readAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The demo design with its blocks registered (OFFSET set as asked).
Document demo(const std::string& offset = "0")
{
    const BlockScan scan = scanBlocks({kDemo + "/blocks"});
    REQUIRE(scan.warnings.empty());
    Document d = loadWokwi(readAll(kDemo + "/design.json")).doc;
    for (Part& p : d.parts)
        if (p.id == "add1")
            p.attrs["OFFSET"] = offset;
    return d;
}

std::string findTool(const char* name)
{
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

TEST_CASE("a block becomes a part type with an auto symbol", "[blocks]")
{
    std::string why;
    const auto b = loadBlock(kDemo + "/blocks/adder4", &why);
    REQUIRE(b);
    CHECK(b->name == "adder4");
    CHECK(b->label == "4-bit adder");
    CHECK(b->prefix == "add");
    REQUIRE(b->verilog.size() == 1);
    CHECK(fs::exists(b->verilog[0]));
    const PartDef d = blockPartDef(*b);
    CHECK(d.type == "chiply-block-adder4");
    CHECK(isExtensionType(d.type));
    CHECK(d.category == "Custom");
    CHECK(d.attrs["OFFSET"] == "0");
    // a0..a3, b0..b3 on the left, sum0..sum4 on the right, bit 0 at the top,
    // all on the 0.1 inch grid.
    std::vector<std::string> names;
    for (const PinDef& p : d.pins) {
        names.push_back(p.name);
        CHECK(std::fabs(std::remainder(p.x, 9.6)) < 1e-9);
        CHECK(std::fabs(std::remainder(p.y, 9.6)) < 1e-9);
    }
    CHECK(names == std::vector<std::string>{"a0", "a1", "a2", "a3", "b0", "b1", "b2", "b3", "sum0", "sum1", "sum2", "sum3", "sum4"});
    CHECK(d.findPin("a0")->x == 0);
    CHECK(d.findPin("sum0")->x == d.width);
    CHECK(d.findPin("a0")->y == d.findPin("sum0")->y);
    CHECK(d.findPin("sum0")->dir == PinDir::Out);
    const auto c = loadBlock(kDemo + "/blocks/counter4", &why);
    REQUIRE(c);
    CHECK(blockPartDef(*c).findPin("clk")->clock);
    CHECK(c->prefix == "counter4_");
}

TEST_CASE("bad block folders are reported, not loaded", "[blocks]")
{
    const fs::path dir = fs::temp_directory_path() / "chiply_bad_blocks";
    fs::remove_all(dir);
    auto make = [&](const char* name, const std::string& json) {
        fs::create_directories(dir / name);
        std::ofstream(dir / name / "block.json") << json;
    };
    make("a", "{ not json");
    make("b", R"({"name": "2bad", "ports": [{"name": "x"}]})");
    make("c", R"({"name": "c", "verilog": ["missing.v"], "ports": [{"name": "x"}]})");
    make("d", R"({"name": "d", "ports": [{"name": "x", "width": 2}, {"name": "x0"}]})");
    make("e", R"({"name": "e", "ports": [{"name": "x", "dir": "sideways"}]})");
    make("ok", R"({"name": "ok", "ports": [{"name": "x"}, {"name": "y", "dir": "out"}]})");
    PartLibrary lib;
    const BlockScan scan = scanBlocks({dir.string()}, lib);
    CHECK(scan.loaded == std::vector<std::string>{"ok"});
    REQUIRE(scan.warnings.size() == 5);
    CHECK(scan.warnings[1].find("Verilog identifier") != std::string::npos);
    CHECK(scan.warnings[2].find("missing.v not found") != std::string::npos);
    CHECK(scan.warnings[3].find("x0 appears twice") != std::string::npos);
    CHECK(lib.find("chiply-block-ok"));
    fs::remove_all(dir);
}

TEST_CASE("a design's own blocks win over the user library", "[blocks]")
{
    const fs::path user = fs::temp_directory_path() / "chiply_user_blocks";
    fs::remove_all(user);
    fs::create_directories(user / "adder4");
    fs::create_directories(user / "only_user");
    std::ofstream(user / "adder4" / "block.json") << R"({"name": "adder4", "label": "user adder", "ports": [{"name": "x"}]})";
    std::ofstream(user / "only_user" / "block.json") << R"({"name": "only_user", "ports": [{"name": "x"}]})";
    PartLibrary lib;
    const BlockScan scan = scanBlocks({kDemo + "/blocks", user.string()}, lib);
    CHECK(lib.find("chiply-block-adder4")->label == "4-bit adder"); // the design's
    CHECK(lib.find("chiply-block-only_user"));
    CHECK(scan.loaded.size() == 3);
    // blockRoots: the design's folder first.
    const auto roots = blockRoots(kDemo + "/design.json");
    REQUIRE(roots.size() == 2);
    CHECK(fs::path(roots[0]) == fs::absolute(kDemo) / "blocks");
    CHECK(roots[1] == defaultUserBlocksDir());
    fs::remove_all(user);
}

TEST_CASE("blocks export as module instances with vectors and parameters", "[blocks]")
{
    const Document d = demo("3");
    VerilogOptions o;
    o.moduleName = "tt_um_blocks";
    const std::string v = writeVerilog(d, PartLibrary::builtin(), o);
    CHECK(v.find("  adder4 #(\n    .OFFSET (3)\n  ) add1 (\n    .a ({net6, net5, net4, net3}),\n") != std::string::npos);
    CHECK(v.find("  wire counter4_1__q3;\n  counter4 counter4_1 (\n    .clk (net1),\n") != std::string::npos);
    CHECK(v.find(".q ({counter4_1__q3, net18, net17, net16})") != std::string::npos);
    const auto files = blockSources(d);
    REQUIRE(files.size() == 2);
    CHECK(fs::path(files[0]).filename() == "adder4.v");
}

TEST_CASE("the exported blocks behave as designed under Icarus", "[blocks]")
{
    const std::string iverilog = findTool("iverilog"), vvp = findTool("vvp");
    if (iverilog.empty() || vvp.empty())
        SKIP("iverilog/vvp not installed");
    const Document d = demo("3");
    const fs::path dir = fs::temp_directory_path() / "chiply_blocks_icarus";
    fs::create_directories(dir);
    VerilogOptions o;
    o.moduleName = "tt_um_blocks";
    std::ofstream(dir / "dut.v") << writeVerilog(d, PartLibrary::builtin(), o);
    std::ofstream(dir / "cells.v") << ttCellsV();
    {
        std::ofstream tb(dir / "tb.v");
        tb << "`timescale 1ns/1ps\nmodule tb;\nreg [7:0] ui_in; reg clk = 0, rst_n = 0; wire [7:0] uo_out, uio_out, uio_oe;\n"
              "integer i, bad = 0;\n"
              "tt_um_blocks dut(.ui_in(ui_in), .uo_out(uo_out), .uio_in(8'h00), .uio_out(uio_out), .uio_oe(uio_oe),"
              " .ena(1'b1), .clk(clk), .rst_n(rst_n));\n"
              "initial begin\n"
              "  for (i = 0; i < 256; i = i + 1) begin ui_in = i; #1;\n"
              "    if (uo_out[4:0] !== ((i % 16) + (i / 16) + 3) % 32) bad = bad + 1; end\n"
              "  #1 rst_n = 1;\n"
              "  for (i = 0; i < 5; i = i + 1) begin #1 clk = 1; #1 clk = 0; end\n"
              "  $display(\"bad=%0d count=%0d\", bad, uo_out[7:5]); $finish;\nend\nendmodule\n";
    }
    std::string cmd = iverilog + " -o " + (dir / "tb.vvp").string() + " " + (dir / "tb.v").string() + " " + (dir / "dut.v").string()
        + " " + (dir / "cells.v").string();
    for (const std::string& f : blockSources(d))
        cmd += " " + f;
    fs::remove(dir / "tb.vvp");
    const std::string build = run(cmd + " 2>&1");
    INFO(build);
    REQUIRE(fs::exists(dir / "tb.vvp"));
    CHECK(run(vvp + " -n " + (dir / "tb.vvp").string()).find("bad=0 count=5") != std::string::npos);
}

TEST_CASE("blocks simulate through the Verilator engine", "[blocks]")
{
    std::string why;
    const auto tools = vl::findTools(&why);
    if (!tools)
        SKIP(why);
    using namespace chiply::sim;
    const Document d = demo("3");
    const Netlist nl = Netlist::build(d, PartLibrary::builtin());
    {
        Simulator builtin(nl);
        bool warned = false;
        for (const std::string& w : builtin.warnings())
            warned |= w.find("custom block adder4; use the Verilator engine") != std::string::npos;
        CHECK(warned);
    }
    vl::BuildOptions bo;
    bo.cacheDir = CHIPLY_VL_TEST_CACHE;
    Options o;
    o.board = false;
    o.flopStart = FlopStart::Zero;
    o.chip = vl::buildChip(d, *tools, bo);
    Simulator sim(nl, o);
    int bad = 0;
    for (unsigned v = 0; v < 256; ++v) {
        for (int i = 0; i < 8; ++i)
            sim.drive(PinRef{"ttin", "IN" + std::to_string(i)}, fromBool((v >> i) & 1));
        REQUIRE(sim.settle());
        unsigned sum = 0;
        for (int i = 0; i < 5; ++i)
            sum |= unsigned(sim.value(PinRef{"ttout", "OUT" + std::to_string(i)}) == V::H) << i;
        bad += sum != ((v % 16) + (v / 16) + 3) % 32;
    }
    CHECK(bad == 0);
    // The counter: reset, then five clock pulses.
    sim.drive(PinRef{"ttin", "RST_N"}, V::L);
    sim.drive(PinRef{"ttin", "CLK"}, V::L);
    REQUIRE(sim.settle());
    sim.drive(PinRef{"ttin", "RST_N"}, V::H);
    for (int i = 0; i < 5; ++i) {
        sim.drive(PinRef{"ttin", "CLK"}, V::H);
        REQUIRE(sim.settle());
        sim.drive(PinRef{"ttin", "CLK"}, V::L);
        REQUIRE(sim.settle());
    }
    unsigned count = 0;
    for (int i = 0; i < 3; ++i)
        count |= unsigned(sim.value(PinRef{"ttout", "OUT" + std::to_string(i + 5)}) == V::H) << i;
    CHECK(count == 5);
}

TEST_CASE("DRC and the Tiny Tapeout export know about blocks", "[blocks]")
{
    Document d = demo();
    drc::Engine e; // Wokwi mode
    e.runFull(d);
    int ext = 0;
    for (const drc::Violation& v : e.violations())
        ext += v.check == "extension-part";
    CHECK(ext == 2);
    // An unconnected block input.
    d.wires.erase(std::remove_if(d.wires.begin(), d.wires.end(), [](const Wire& w) { return w.to.str() == "add1:b3"; }),
                  d.wires.end());
    e.setExtensionsAllowed(true);
    e.runFull(d);
    REQUIRE(e.violations().size() == 1);
    CHECK(e.violations()[0].key == "unconnected-input:add1:b3");

    const fs::path dir = fs::temp_directory_path() / "chiply_blocks_project";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "info.yaml") << "project:\n  language: \"Wokwi\"\n";
    VerilogOptions o;
    o.moduleName = "tt_um_blocks";
    exportTtProject(demo(), PartLibrary::builtin(), dir.string(), o);
    CHECK(fs::exists(dir / "src" / "adder4.v"));
    CHECK(fs::exists(dir / "src" / "counter4.v"));
    const std::string yaml = readAll(dir / "info.yaml");
    CHECK(yaml.find("    - \"adder4.v\"\n    - \"counter4.v\"\n") != std::string::npos);
    fs::remove_all(dir);
}
