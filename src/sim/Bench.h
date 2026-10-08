#pragma once
// A benchmark of the engine on one design (PLAN.md 13): the same stages the
// editor runs, timed, so native, Node and browser builds can be compared.
#include <string>
#include <vector>

namespace chiply::sim {

struct BenchOptions {
    double boardSeconds = 1.0;       // simulated time to run with the clock generators
    int randomCycles = 2000;         // chip-only cycles with random inputs (0: skip)
    struct SwitchSet {
        std::string part;
        int index = 0;
        bool on = true;
    };
    std::vector<SwitchSet> switches; // set before the board run (e.g. the clock's slide switch)
    unsigned seed = 12345;
};

struct BenchResult {
    int parts = 0, wires = 0, nets = 0;
    double parseMs = 0;     // JSON -> Document
    double netlistMs = 0;   // Netlist::build (of the flattened design)
    double drcMs = 0;       // a full DRC pass
    double compileMs = 0;   // Simulator construction (compile + initialise)
    double boardMs = 0;     // wall time of the board run
    double boardSeconds = 0; // simulated time of the board run
    unsigned long long boardEvals = 0;
    double randomMs = 0;
    int randomCycles = 0;
    unsigned long long randomEvals = 0;
    std::string error;      // "" if everything ran

    std::string text() const; // a readable report
    std::string json() const; // the same as JSON
};

// `json` is the diagram file's contents.
BenchResult runBench(const std::string& json, const BenchOptions& opt);

} // namespace chiply::sim
