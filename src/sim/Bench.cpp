#include "sim/Bench.h"

#include "core/Drc.h"
#include "core/Netlist.h"
#include "core/Sheets.h"
#include "core/WokwiJson.h"
#include "sim/Simulator.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <sstream>

namespace chiply::sim {

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

std::string fmt(double v, int decimals = 1)
{
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    return b;
}

} // namespace

BenchResult runBench(const std::string& json, const BenchOptions& opt)
{
    BenchResult r;
    try {
        auto t0 = Clock::now();
        Document doc = loadWokwi(json).doc;
        r.parseMs = msSince(t0);
        r.parts = int(doc.parts.size());
        r.wires = int(doc.wires.size());

        t0 = Clock::now();
        const Document flat = flattenSheets(doc);
        const Netlist nl = Netlist::build(flat, PartLibrary::builtin());
        r.netlistMs = msSince(t0);
        r.nets = int(nl.connectedNetCount());

        t0 = Clock::now();
        drc::Engine drcEngine;
        drcEngine.setExtensionsAllowed(true);
        drcEngine.runFull(doc);
        r.drcMs = msSince(t0);

        // The board run: everything as drawn, clock generators running.
        {
            t0 = Clock::now();
            Options o;
            o.seed = opt.seed;
            Simulator sim(nl, o);
            r.compileMs = msSince(t0);
            for (const BenchOptions::SwitchSet& s : opt.switches)
                sim.setSwitch(s.part, s.index, s.on);
            const Time total = Time(opt.boardSeconds * 1e12);
            const Time step = 1'000'000'000; // 1 ms, as the editor's ticks do
            t0 = Clock::now();
            const auto evals0 = sim.evaluations();
            for (Time t = 0; t < total; t += step)
                if (!sim.advance(std::min(step, total - t))) {
                    r.error = sim.lastError();
                    break;
                }
            r.boardMs = msSince(t0);
            r.boardSeconds = opt.boardSeconds;
            r.boardEvals = sim.evaluations() - evals0;
        }

        // The random run: the chip only, random inputs, a clock pulse per
        // cycle (as the co-simulation test drives it).
        if (opt.randomCycles > 0) {
            std::string inId;
            std::vector<std::string> uio(8);
            // Without a Tiny Tapeout input block, the design's own switches
            // and buttons are worked instead (board mode).
            std::vector<std::pair<std::string, int>> switches;
            std::vector<std::string> buttons;
            for (const Device& d : nl.devices) {
                if (inId.empty() && (d.type == "board-tt-block-input" || d.type == "board-tt-block-input-8"))
                    inId = d.partId;
                if (d.type == "wokwi-dip-switch-8")
                    for (int i = 0; i < 8; ++i)
                        switches.push_back({d.partId, i});
                else if (d.type == "wokwi-slide-switch")
                    switches.push_back({d.partId, 0});
                else if (d.type == "wokwi-pushbutton")
                    buttons.push_back(d.partId);
                if (d.type == "board-tt-block-bidirectional-io" && d.attrs && d.attrs->contains("verilogBit")) {
                    const Json& bit = (*d.attrs)["verilogBit"];
                    const std::string s = bit.is_string() ? bit.get<std::string>() : bit.dump();
                    if (s.size() == 1 && s[0] >= '0' && s[0] <= '7')
                        uio[size_t(s[0] - '0')] = d.partId;
                }
            }
            const bool chipOnly = !inId.empty();
            Options o;
            o.board = !chipOnly;
            o.flopStart = FlopStart::Zero;
            Simulator sim(nl, o);
            std::mt19937 rng(opt.seed);
            t0 = Clock::now();
            const auto evals0 = sim.evaluations();
            for (int c = 0; c < opt.randomCycles; ++c) {
                const unsigned v = rng() & 0xff, w = rng() & 0xff;
                if (!inId.empty()) {
                    for (int b = 0; b < 8; ++b)
                        sim.drive(PinRef{inId, "IN" + std::to_string(b)}, fromBool((v >> b) & 1));
                    sim.drive(PinRef{inId, "RST_N"}, fromBool(c >= 4));
                    sim.drive(PinRef{inId, "CLK"}, V::L);
                }
                for (int b = 0; b < 8; ++b)
                    if (!uio[size_t(b)].empty())
                        sim.drive(PinRef{uio[size_t(b)], "IN"}, fromBool((w >> b) & 1));
                if (!chipOnly) {
                    for (const auto& [part, index] : switches)
                        sim.setSwitch(part, index, rng() & 1);
                    for (const std::string& b : buttons)
                        sim.setPressed(b, rng() & 1);
                }
                if (!sim.settle()) {
                    r.error = sim.lastError();
                    break;
                }
                if (!inId.empty())
                    sim.drive(PinRef{inId, "CLK"}, V::H);
                if (!sim.settle()) {
                    r.error = sim.lastError();
                    break;
                }
                ++r.randomCycles;
            }
            r.randomMs = msSince(t0);
            r.randomEvals = sim.evaluations() - evals0;
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

std::string BenchResult::text() const
{
    std::ostringstream o;
    o << "design: " << parts << " parts, " << wires << " wires, " << nets << " nets\n"
      << "parse JSON:        " << fmt(parseMs) << " ms\n"
      << "netlist:           " << fmt(netlistMs) << " ms\n"
      << "DRC (full):        " << fmt(drcMs) << " ms\n"
      << "simulator compile: " << fmt(compileMs) << " ms\n";
    if (boardSeconds > 0) {
        const double ratio = boardMs > 0 ? boardSeconds * 1000.0 / boardMs : 0;
        o << "board run:         " << fmt(boardMs) << " ms for " << fmt(boardSeconds, 3) << " s simulated ("
          << fmt(ratio) << "x real time, " << boardEvals << " evaluations, "
          << fmt(boardMs > 0 ? double(boardEvals) / boardMs / 1000.0 : 0, 2) << " M/s)\n";
    }
    if (randomCycles > 0)
        o << "random run:        " << fmt(randomMs) << " ms for " << randomCycles << " cycles ("
          << fmt(randomMs > 0 ? randomCycles / randomMs * 1000.0 : 0, 0) << " cycles/s, " << randomEvals << " evaluations, "
          << fmt(randomMs > 0 ? double(randomEvals) / randomMs / 1000.0 : 0, 2) << " M/s)\n";
    if (!error.empty())
        o << "error: " << error << "\n";
    return o.str();
}

std::string BenchResult::json() const
{
    Json j = {{"parts", parts},         {"wires", wires},           {"nets", nets},
              {"parseMs", parseMs},     {"netlistMs", netlistMs},   {"drcMs", drcMs},
              {"compileMs", compileMs}, {"boardMs", boardMs},       {"boardSeconds", boardSeconds},
              {"boardEvals", boardEvals}, {"randomMs", randomMs},   {"randomCycles", randomCycles},
              {"randomEvals", randomEvals}, {"error", error}};
    return j.dump();
}

} // namespace chiply::sim
