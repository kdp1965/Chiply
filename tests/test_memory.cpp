// RAM and ROM (PLAN.md 7.3): types, contents, built-in simulation, Verilog
// export under Icarus, the Verilator engine, DRC and the project export.
#include "core/Drc.h"
#include "core/Memory.h"
#include "core/Netlist.h"
#include "core/Verilog.h"
#include "core/WokwiJson.h"
#include "sim/Simulator.h"
#include "vl/VerilatorChip.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

using namespace chiply;
using namespace chiply::sim;
namespace fs = std::filesystem;

namespace {

const char* kSevenSeg = "3f 06 5b 4f 66 6d 7d 07 // 0..7\n7f 6f 77 7c 39 5e 79 71 /* 8..f */";
const unsigned kSevenSegWords[16] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07,
                                     0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71};

Document doc(const std::string& parts, const std::string& wires)
{
    return loadWokwi(R"({"version": 1, "author": "", "editor": "wokwi", "parts": [)"
                     R"({"type": "board-tt-block-input", "id": "ttin", "top": 0, "left": -250, "attrs": {}},)"
                     R"({"type": "board-tt-block-output", "id": "ttout", "top": 0, "left": 350, "attrs": {}},)"
                     + parts + R"(], "connections": [)" + wires + R"(], "dependencies": {}})")
        .doc;
}

std::string w(const std::string& a, const std::string& b) { return R"([")" + a + R"(", ")" + b + R"(", "green", []])"; }

// ui_in[3:0] -> ROM address, ROM data -> uo_out.
Document romDesign(const std::string& attrs)
{
    std::string wires;
    for (int i = 0; i < 4; ++i)
        wires += w("ttin:IN" + std::to_string(i), "rom1:a" + std::to_string(i)) + ",";
    for (int i = 0; i < 8; ++i)
        wires += w("rom1:q" + std::to_string(i), "ttout:OUT" + std::to_string(i)) + (i < 7 ? "," : "");
    return doc(R"({"type": "chiply-rom-16x8", "id": "rom1", "top": 0, "left": 0, "attrs": )" + attrs + "}", wires);
}

// RAM 8x4: a = ui_in[2:0], d = ui_in[6:3], we = ui_in[7], clk = CLK; q -> uo_out[3:0].
Document ramDesign()
{
    std::string wires = w("ttin:CLK", "ram1:clk") + "," + w("ttin:IN7", "ram1:we") + ",";
    for (int i = 0; i < 3; ++i)
        wires += w("ttin:IN" + std::to_string(i), "ram1:a" + std::to_string(i)) + ",";
    for (int i = 0; i < 4; ++i)
        wires += w("ttin:IN" + std::to_string(i + 3), "ram1:d" + std::to_string(i)) + ",";
    for (int i = 0; i < 4; ++i)
        wires += w("ram1:q" + std::to_string(i), "ttout:OUT" + std::to_string(i)) + (i < 3 ? "," : "");
    return doc(R"({"type": "chiply-ram-8x4", "id": "ram1", "top": 0, "left": 0, "attrs": {}})", wires);
}

struct Chip {
    Netlist nl;
    std::unique_ptr<Simulator> sim;
    Chip(const Document& d, Options o)
        : nl(Netlist::build(d, PartLibrary::builtin()))
    {
        o.board = false;
        sim = std::make_unique<Simulator>(nl, o);
    }
    void set(unsigned ui, bool clk)
    {
        for (int i = 0; i < 8; ++i)
            sim->drive(PinRef{"ttin", "IN" + std::to_string(i)}, fromBool((ui >> i) & 1));
        sim->drive(PinRef{"ttin", "CLK"}, fromBool(clk));
        REQUIRE(sim->settle());
    }
    std::string out(int bits = 8) const
    {
        std::string s;
        for (int i = bits - 1; i >= 0; --i)
            s += toChar(sim->value(PinRef{"ttout", "OUT" + std::to_string(i)}));
        return s;
    }
};

// A write/read sequence for the RAM: write data at every address, then read
// them all back, with a few writes masked by we = 0. Each step: inputs, then
// a clock pulse; the output is sampled after the pulse.
std::vector<unsigned> ramSequence()
{
    std::vector<unsigned> seq;
    for (unsigned a = 0; a < 8; ++a)
        seq.push_back(0x80 | (((a * 5 + 3) & 15) << 3) | a); // write
    seq.push_back(0x00 | (0xf << 3) | 2);                   // we = 0: no write
    for (unsigned a = 0; a < 8; ++a)
        seq.push_back(a); // read
    return seq;
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

TEST_CASE("memory part types", "[memory]")
{
    auto m = parseMemoryType("chiply-ram-64x12");
    REQUIRE(m);
    CHECK(!m->rom);
    CHECK(m->depth == 64);
    CHECK(m->width == 12);
    CHECK(m->abits == 6);
    CHECK(parseMemoryType("chiply-rom-2x1"));
    CHECK(!parseMemoryType("chiply-ram-24x8"));  // not a power of two
    CHECK(!parseMemoryType("chiply-ram-512x8")); // too deep
    CHECK(!parseMemoryType("chiply-rom-16x17")); // too wide
    CHECK(!parseMemoryType("chiply-ram-16"));
    CHECK(memoryType(true, 32, 4) == "chiply-rom-32x4");

    const PartDef* ram = PartLibrary::builtin().find("chiply-ram-16x8");
    REQUIRE(ram);
    CHECK(!ram->hidden); // offered in Add Part
    CHECK(ram->findPin("clk")->clock);
    CHECK(ram->findPin("a3"));
    CHECK(!ram->findPin("a4"));
    CHECK(ram->findPin("d7")->dir == PinDir::In);
    CHECK(ram->findPin("q7")->dir == PinDir::Out);
    CHECK(ram->prefix == "ram");
    const PartDef* rom = PartLibrary::builtin().find("chiply-rom-8x3"); // made on first use
    REQUIRE(rom);
    CHECK(rom->hidden);
    CHECK(rom->findPin("a2"));
    CHECK(!rom->findPin("clk"));
    CHECK(rom->attrs.contains("data"));
    CHECK(PartLibrary::builtin().find("chiply-rom-8x3") == rom); // the same definition next time
}

TEST_CASE("memory contents", "[memory]")
{
    MemoryInfo m;
    m.rom = true;
    m.depth = 16;
    m.width = 8;
    m.abits = 4;
    std::string err;
    auto words = parseMemoryText(kSevenSeg, m, &err);
    CHECK(err.empty());
    for (int i = 0; i < 16; ++i)
        CHECK(words[size_t(i)] == kSevenSegWords[i]);
    words = parseMemoryText("@4 aa, bb\n@0 1_1", m, &err);
    CHECK(err.empty());
    CHECK(words[0] == 0x11);
    CHECK(words[4] == 0xaa);
    CHECK(words[5] == 0xbb);
    CHECK(words[1] == 0);
    parseMemoryText("100", m, &err);
    CHECK(err.find("wider than 8 bits") != std::string::npos);
    err.clear();
    parseMemoryText("@f 1 2", m, &err);
    CHECK(err.find("more than 16 words") != std::string::npos);
    err.clear();
    parseMemoryText("12 zz", m, &err);
    CHECK(err.find("unexpected") != std::string::npos);

    // A file next to the design.
    const fs::path dir = fs::temp_directory_path() / "chiply_rom_file";
    fs::create_directories(dir);
    std::ofstream(dir / "digits.hex") << kSevenSeg;
    Part p;
    p.attrs = {{"file", "digits.hex"}};
    err.clear();
    words = memoryContents(p, m, dir.string(), &err);
    CHECK(err.empty());
    CHECK(words[15] == 0x71);
    p.attrs = {{"file", "missing.hex"}};
    memoryContents(p, m, dir.string(), &err);
    CHECK(err.find("cannot read") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("ROM and RAM in the built-in simulator", "[memory]")
{
    {
        Chip c(romDesign(std::string(R"({"data": ")") + "3f 06 5b 4f 66 6d 7d 07 7f 6f 77 7c 39 5e 79 71" + R"("})"), {});
        for (unsigned a = 0; a < 16; ++a) {
            c.set(a, false);
            unsigned got = 0;
            for (int i = 0; i < 8; ++i)
                got |= unsigned(c.sim->value(PinRef{"ttout", "OUT" + std::to_string(i)}) == V::H) << i;
            CHECK(got == kSevenSegWords[a]);
        }
    }
    Options zero;
    zero.flopStart = FlopStart::Zero;
    Chip c(ramDesign(), zero);
    c.set(5, false);
    CHECK(c.out(4) == "0000"); // starts at zero
    c.set(0x80 | (0xa << 3) | 5, false); // write 1010 at 5 on the next edge
    CHECK(c.out(4) == "0000");           // not before the edge
    c.set(0x80 | (0xa << 3) | 5, true);
    CHECK(c.out(4) == "1010"); // written on the rising edge, read at once
    c.set(0x00 | (0x3 << 3) | 5, false);
    c.set(0x00 | (0x3 << 3) | 5, true); // we = 0: no write
    CHECK(c.out(4) == "1010");
    c.set(4, false);
    CHECK(c.out(4) == "0000"); // the read follows the address
    c.set(5, false);
    CHECK(c.out(4) == "1010");

    Options unknown;
    unknown.flopStart = FlopStart::Unknown;
    unknown.wokwiLogic = false;
    Chip u(ramDesign(), unknown);
    u.set(1, false);
    CHECK(u.out(4) == "xxxx");
    // An unknown address: every word may be written.
    u.sim->drive(PinRef{"ttin", "IN0"}, V::X);
    u.sim->drive(PinRef{"ttin", "IN7"}, V::H);
    REQUIRE(u.sim->settle());
    Options rnd;
    rnd.seed = 7;
    Chip r1(ramDesign(), rnd), r2(ramDesign(), rnd);
    std::string a, b;
    for (unsigned i = 0; i < 8; ++i) {
        r1.set(i, false);
        r2.set(i, false);
        a += r1.out(4);
        b += r2.out(4);
    }
    CHECK(a == b);                 // a seed repeats
    CHECK(a != std::string(32, '0')); // random, not all zero
}

TEST_CASE("exported memories behave the same under Icarus", "[memory]")
{
    const std::string iverilog = findTool("iverilog"), vvp = findTool("vvp");
    if (iverilog.empty() || vvp.empty())
        SKIP("iverilog/vvp not installed");
    const fs::path dir = fs::temp_directory_path() / "chiply_memory_icarus";
    fs::create_directories(dir);
    std::ofstream(dir / "cells.v") << ttCellsV();
    std::ofstream(dir / "chiply_cells.v") << chiplyCellsV();
    auto icarus = [&](const Document& d, const std::vector<unsigned>& seq) {
        VerilogOptions o;
        o.moduleName = "tt_um_mem";
        std::ofstream(dir / "dut.v") << writeVerilog(d, PartLibrary::builtin(), o);
        {
            std::ofstream tb(dir / "tb.v");
            tb << "`timescale 1ns/1ps\nmodule tb;\nreg [7:0] ui_in; reg clk = 0; wire [7:0] uo_out, uio_out, uio_oe;\n"
                  "tt_um_mem dut(.ui_in(ui_in), .uo_out(uo_out), .uio_in(8'h00), .uio_out(uio_out), .uio_oe(uio_oe),"
                  " .ena(1'b1), .clk(clk), .rst_n(1'b1));\ninitial begin\n";
            for (unsigned v : seq)
                tb << "  ui_in = " << v << "; #1 clk = 1; #1 $display(\"%b\", uo_out); clk = 0; #1;\n";
            tb << "  $finish;\nend\nendmodule\n";
        }
        fs::remove(dir / "tb.vvp");
        const std::string build = run(iverilog + " -o " + (dir / "tb.vvp").string() + " " + (dir / "tb.v").string() + " "
                                      + (dir / "dut.v").string() + " " + (dir / "cells.v").string() + " "
                                      + (dir / "chiply_cells.v").string() + " 2>&1");
        INFO(build);
        REQUIRE(fs::exists(dir / "tb.vvp"));
        std::istringstream in(run(vvp + " -n " + (dir / "tb.vvp").string()));
        std::vector<std::string> lines;
        for (std::string l; std::getline(in, l);)
            if (!l.empty() && l.find("finish") == std::string::npos && l.find("VCD") == std::string::npos)
                lines.push_back(l);
        return lines;
    };
    auto chiply = [&](const Document& d, const std::vector<unsigned>& seq) {
        Options o;
        o.flopStart = FlopStart::Unknown; // like Verilog regs
        o.wokwiLogic = false;
        Chip c(d, o);
        std::vector<std::string> lines;
        for (unsigned v : seq) {
            c.set(v, false);
            c.set(v, true);
            lines.push_back(c.out());
        }
        return lines;
    };
    const Document rom = romDesign(R"({"data": "3f 06 5b 4f 66 6d 7d 07 7f 6f 77 7c 39 5e 79 71"})");
    std::vector<unsigned> romSeq;
    for (unsigned a = 0; a < 16; ++a)
        romSeq.push_back(a);
    CHECK(icarus(rom, romSeq) == chiply(rom, romSeq));
    const Document ram = ramDesign();
    std::vector<std::string> got = icarus(ram, ramSequence()), want = chiply(ram, ramSequence());
    REQUIRE(got.size() == 17);
    REQUIRE(want.size() == 17);
    // uo_out[7:4] are not connected: the export ties them to 0, Chiply
    // shows them floating. Compare the RAM's four bits.
    for (std::size_t i = 0; i < got.size(); ++i)
        CHECK(got[i].substr(4) == want[i].substr(4));
    CHECK(got[16].substr(4) == "0110"); // address 7 holds (7*5+3)&15 = 6
}

TEST_CASE("memories through the Verilator engine", "[memory]")
{
    std::string why;
    const auto tools = vl::findTools(&why);
    if (!tools)
        SKIP(why);
    vl::BuildOptions bo;
    bo.cacheDir = CHIPLY_VL_TEST_CACHE;
    for (const Document& d : {romDesign(R"({"data": "3f 06 5b 4f 66 6d 7d 07 7f 6f 77 7c 39 5e 79 71"})"), ramDesign()}) {
        Options o;
        o.flopStart = FlopStart::Zero;
        Options ov = o;
        ov.chip = vl::buildChip(d, *tools, bo);
        Chip ref(d, o), vlc(d, ov);
        std::vector<unsigned> seq = ramSequence();
        for (unsigned a = 0; a < 16; ++a)
            seq.push_back(a);
        for (unsigned v : seq) {
            ref.set(v, false);
            vlc.set(v, false);
            ref.set(v, true);
            vlc.set(v, true);
            CHECK(vlc.out() == ref.out());
        }
    }
}

TEST_CASE("DRC and the export know about memories", "[memory]")
{
    drc::Engine e;
    e.setExtensionsAllowed(true);
    e.runFull(romDesign(R"({"data": "3f 100"})"));
    REQUIRE(e.violations().size() == 1);
    CHECK(e.violations()[0].check == "memory-contents");
    CHECK(e.violations()[0].message.find("wider than 8 bits") != std::string::npos);
    e.runFull(romDesign(R"({"file": "no-such-file.hex"})"));
    REQUIRE(e.violations().size() == 1);
    CHECK(e.violations()[0].check == "memory-contents");
    e.runFull(ramDesign());
    CHECK(e.count(drc::Severity::Error) == 0); // (uo_out[7:4] unconnected: warnings only)

    const fs::path dir = fs::temp_directory_path() / "chiply_memory_project";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "info.yaml") << "project:\n  language: \"Wokwi\"\n";
    VerilogOptions o;
    o.moduleName = "tt_um_mem";
    exportTtProject(ramDesign(), PartLibrary::builtin(), dir.string(), o);
    CHECK(fs::exists(dir / "src" / "chiply_cells.v")); // chiply_ram lives there
    const std::string v = writeVerilog(romDesign(R"({"data": "3f 06"})"), PartLibrary::builtin(), o);
    CHECK(v.find("  tt_um_mem_rom1_rom rom1 (\n    .addr ({net4, net3, net2, net1}),\n") != std::string::npos);
    CHECK(v.find("module tt_um_mem_rom1_rom (\n    input  wire [3:0] addr,\n    output reg  [7:0] dout\n);") != std::string::npos);
    CHECK(v.find("            4'd1: dout = 8'h6;\n") != std::string::npos);
    CHECK(!usesChiplyCells(romDesign("{}"))); // a ROM needs nothing extra
    CHECK_THROWS_AS(writeVerilog(romDesign(R"({"data": "zz"})"), PartLibrary::builtin(), o), ExportError);
    fs::remove_all(dir);
}
