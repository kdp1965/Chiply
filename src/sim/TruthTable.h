#pragma once
// Tiny Tapeout truth tables (PLAN.md 6.4): the truthtable.md format that
// tt-support-tools downloads with a Wokwi project and checks with cocotb
// (testing/lib/testutils/truthtable.py), run on the built-in simulator.
//
// A markdown table; everything up to the |---|---| line is a header. Each
// row is | inputs | expected outputs | comment |, 8 characters MSB first
// (spaces ignored, optional 8' or 0b prefix):
//   inputs (ui_in):   0 1 = set, t = toggle, c = clock, x - = unchanged
//   outputs (uo_out): 0 1 = expect, x - = don't care; empty = no check
// A row with c bits expands as in truthtable.py: a setup step (c bits
// unchanged, t bits toggled), two steps that toggle the c bits (a full clock
// pulse), then the row itself (t bits toggled again, so a t in a clocked row
// is a pulse around the clock), whose outputs are checked. Each step sets
// ui_in, waits 10 ns and compares uo_out. Yosys-style rows
// "8'11111000 | 8'xxxxxxx0" are read too.
#include "sim/Simulator.h"

#include <string>
#include <vector>

namespace chiply::sim {

struct TruthStep {
    std::string in;   // 8 chars '0'/'1', bit 7 first
    std::string out;  // 8 chars '0'/'1'/'-', bit 7 first; empty = no check
    int line = 0;     // source line (1-based)
    std::string comment;
};

struct TruthTable {
    std::vector<TruthStep> steps;
    std::vector<std::string> warnings;
};

TruthTable parseTruthTable(const std::string& text);

struct TruthResult {
    int steps = 0;
    int checked = 0;
    std::vector<std::string> failures; // one line each, with the source line
};

// in / out: 8 nets each, index 0 = bit 0 (ui_in[0], uo_out[0]).
TruthResult runTruthTable(Simulator& sim, const TruthTable& table, const std::vector<int>& in,
                          const std::vector<int>& out, Time stepDelay = 10'000);

} // namespace chiply::sim
