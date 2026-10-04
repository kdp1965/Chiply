#pragma once
// Optional Verilator backend (PLAN.md 6.6, M9).
//
// The chip is exported to Verilog (core/Verilog), turned into C++ by
// Verilator, compiled with the system C++ compiler into a shared library for
// this machine's architecture, and loaded into Chiply. Verilator's own
// architecture does not matter (an Intel Verilator under Rosetta works on
// an Apple-silicon Mac): only its generated C++ and runtime sources are used.
//
// Builds are cached by a hash of everything that goes into them, so running
// the same design again starts at once. Nothing here is needed to use
// Chiply: without Verilator the built-in simulator does everything.
#include "core/Document.h"
#include "sim/ChipBackend.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace chiply::vl {

struct Tools {
    std::string verilator;  // executable
    std::string root;       // VERILATOR_ROOT (include/verilated.cpp lives here)
    std::string version;    // "5.050"
    std::string cxx;        // C++ compiler
};

// Looks for Verilator ($CHIPLY_VERILATOR, then PATH, then the usual
// Homebrew / MacPorts / system directories) and a C++ compiler. On failure
// returns nullopt and says why.
std::optional<Tools> findTools(std::string* why = nullptr);

struct BuildError : std::runtime_error {
    using std::runtime_error::runtime_error;
    std::string log; // compiler / Verilator output
};

struct BuildOptions {
    std::string cacheDir;                          // default: defaultCacheDir()
    std::function<void(const std::string&)> status; // progress lines (any thread)
    const std::atomic<bool>* cancel = nullptr;      // stop between steps
    int jobs = 0;                                   // parallel compiles; 0 = all cores
    std::string baseDir;                            // the design's folder (ROM files)
};

struct BuildInfo {
    bool fromCache = false;
    double seconds = 0;
    std::string library; // path of the loaded shared library
};

// Builds (or loads from the cache) the chip of `doc` and returns a backend
// for Simulator Options::chip. Throws BuildError (or ExportError for parts
// with no Verilog cell).
std::shared_ptr<sim::ChipBackend> buildChip(const Document& doc, const Tools& tools, const BuildOptions& opt = {},
                                            BuildInfo* info = nullptr);

// $CHIPLY_VL_CACHE if set, else ~/Library/Caches/Chiply/verilator on macOS, $XDG_CACHE_HOME (or
// ~/.cache)/chiply/verilator elsewhere.
std::string defaultCacheDir();

} // namespace chiply::vl
