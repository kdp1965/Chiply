// Co-simulation: Chiply's simulator vs Icarus Verilog running Wokwi's own
// Verilog export of the reference design (reference/tt_um_wokwi_*.v +
// cells.v), with the same random stimulus. Skipped if iverilog is absent.
#include "core/Netlist.h"
#include "core/WokwiJson.h"
#include "sim/Simulator.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <random>
#include <sstream>

using namespace chiply;
using namespace chiply::sim;
namespace fs = std::filesystem;

namespace {

std::string findTool(const char* name)
{
    if (const char* env = std::getenv("CHIPLY_IVERILOG_DIR"))
        if (fs::exists(fs::path(env) / name))
            return (fs::path(env) / name).string();
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

struct Vec {
    unsigned ui, uio;
    bool rstn;
};

} // namespace

TEST_CASE("built-in simulator matches Icarus on Wokwi's Verilog export")
{
    const std::string iverilog = findTool("iverilog"), vvp = findTool("vvp");
    if (iverilog.empty() || vvp.empty())
        SKIP("iverilog/vvp not installed");

    const std::string ref = CHIPLY_REFERENCE_DIR;
    const int cycles = 3000;
    std::mt19937 rng(12345);
    std::vector<Vec> vec;
    for (int i = 0; i < cycles; ++i)
        vec.push_back({unsigned(rng() & 0xff), unsigned(rng() & 0xff), !(i < 4 || rng() % 100 == 0)});

    // --- Icarus run
    const fs::path dir = fs::temp_directory_path() / "chiply_cosim";
    fs::create_directories(dir);
    {
        std::ofstream tb(dir / "tb.v");
        tb << "`timescale 1ns/1ps\nmodule tb;\n"
              "reg [7:0] ui_in, uio_in; reg clk = 0, rst_n = 0;\n"
              "wire [7:0] uo_out, uio_out, uio_oe;\n"
              "tt_um_wokwi_414123795172381697 dut(.ui_in(ui_in), .uo_out(uo_out), .uio_in(uio_in), .uio_out(uio_out),"
              " .uio_oe(uio_oe), .ena(1'b1), .clk(clk), .rst_n(rst_n));\n"
              "initial begin\n";
        for (const Vec& v : vec) {
            char line[256];
            std::snprintf(line, sizeof line,
                          "  ui_in = 8'h%02x; uio_in = 8'h%02x; rst_n = %d; #4 $display(\"%%b %%b %%b\", uo_out, uio_out, uio_oe);"
                          " #1 clk = 1; #4 $display(\"%%b %%b %%b\", uo_out, uio_out, uio_oe); #1 clk = 0;\n",
                          v.ui, v.uio, v.rstn ? 1 : 0);
            tb << line;
        }
        tb << "  $finish;\nend\nendmodule\n";
    }
    const std::string build = run(iverilog + " -o " + (dir / "tb.vvp").string() + " " + (dir / "tb.v").string() + " " + ref +
                                  "/tt_um_wokwi_414123795172381697.v " + ref + "/cells.v 2>&1");
    INFO(build);
    REQUIRE(fs::exists(dir / "tb.vvp"));
    std::istringstream expected(run(vvp + " -n " + (dir / "tb.vvp").string()));

    // --- Chiply run (flip-flops start unknown, like Verilog regs)
    std::ifstream in(ref + "/wokwi_414123795172381697.diagram.json");
    std::stringstream ss;
    ss << in.rdbuf();
    const Document doc = loadWokwi(ss.str()).doc;
    const Netlist nl = Netlist::build(doc, PartLibrary::builtin());
    Options opt;
    opt.flopStart = FlopStart::Unknown;
    opt.board = false; // drive the chip's pins directly, like the Verilog testbench
    opt.wokwiLogic = false; // Verilog four-state semantics
    Simulator sim(nl, opt);

    std::map<std::string, std::string> uio; // bit -> part (last wins, like the export)
    for (const Part& p : doc.parts)
        if (p.type == "board-tt-block-bidirectional-io")
            uio[p.attrs.value("verilogBit", std::string())] = p.id;
    auto bus = [&](auto pinOfBit) {
        std::string s;
        for (int b = 7; b >= 0; --b)
            s += toChar(sim.value(pinOfBit(b)));
        return s;
    };
    auto sample = [&] {
        return bus([](int b) { return PinRef{"ttout", "OUT" + std::to_string(b)}; }) + " " +
               bus([&](int b) { return PinRef{uio[std::to_string(b)], "OUT"}; }) + " " +
               bus([&](int b) { return PinRef{uio[std::to_string(b)], "OE"}; });
    };

    int compared = 0, hard = 0, soft = 0, known = 0, bits = 0;
    std::set<std::string> distinctUo;
    std::string firstHard;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < cycles; ++i) {
        const Vec& v = vec[size_t(i)];
        for (int b = 0; b < 8; ++b) {
            sim.drive(PinRef{"ttin", "IN" + std::to_string(b)}, fromBool((v.ui >> b) & 1));
            sim.drive(PinRef{uio[std::to_string(b)], "IN"}, fromBool((v.uio >> b) & 1));
        }
        sim.drive(PinRef{"ttin", "RST_N"}, fromBool(v.rstn));
        sim.drive(PinRef{"ttin", "CLK"}, V::L);
        REQUIRE(sim.settle());
        for (int phase = 0; phase < 2; ++phase) {
            if (phase == 1) {
                sim.drive(PinRef{"ttin", "CLK"}, V::H);
                REQUIRE(sim.settle());
            }
            std::string want;
            std::getline(expected, want);
            const std::string got = sample();
            ++compared;
            for (char c : want)
                if (c != ' ') {
                    ++bits;
                    known += (c == '0' || c == '1');
                }
            distinctUo.insert(want.substr(0, 8));
            for (std::size_t k = 0; k < got.size() && k < want.size(); ++k) {
                if (got[k] == want[k])
                    continue;
                const bool refKnown = want[k] == '0' || want[k] == '1';
                if (refKnown) {
                    ++hard;
                    if (firstHard.empty())
                        firstHard = "cycle " + std::to_string(i) + (phase ? " after" : " before") +
                                    " edge: Chiply " + got + " vs Icarus " + want;
                } else {
                    ++soft; // Icarus says x/z; Chiply is more definite
                }
            }
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    WARN("compared " << compared << " samples (" << cycles << " cycles) in " << ms << " ms; "
                     << sim.evaluations() << " evaluations; x-only differences: " << soft);
    INFO(firstHard);
    CHECK(compared == 2 * cycles);
    // The comparison must be meaningful, not a match of all-X outputs.
    CHECK(known * 2 > bits);
    CHECK(distinctUo.size() > 50);
    CHECK(hard == 0);
}
