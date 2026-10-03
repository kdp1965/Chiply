#pragma once
// Another engine that simulates the chip (PLAN.md 6.6): the logic cells of
// a design, seen from the Tiny Tapeout pins. The built-in simulator keeps
// the board (pads, switches, buttons, clock, displays) and hands the chip
// its inputs; the backend reports the value of every chip net it computes,
// so wires, flip-flop squares and tooltips stay live.
#include "sim/Value.h"

#include <cstdint>
#include <string>
#include <vector>

namespace chiply::sim {

enum class FlopStart;

class ChipBackend {
public:
    virtual ~ChipBackend() = default;
    virtual std::string name() const = 0; // e.g. "Verilator 5.050"
    // Netlist net indices the backend drives (chip nets that are not chip
    // inputs or constants), in the order read() reports them. They refer to
    // Netlist::build() of the same document the backend was built from.
    virtual const std::vector<int>& nets() const = 0;
    // A fresh chip: flip-flops start per `start` (Unknown is treated as
    // Random: a two-state engine), seed 0 = new random seed.
    virtual void reset(FlopStart start, std::uint64_t seed) = 0;
    // Applies the Tiny Tapeout inputs and evaluates (clock edges included).
    virtual void eval(std::uint8_t uiIn, std::uint8_t uioIn, bool clk, bool rstN) = 0;
    // 0/1 per nets().
    virtual void read(std::vector<std::uint8_t>& values) = 0;
};

} // namespace chiply::sim
