#pragma once
// Verilog export (PLAN.md 5.3), in the shape of Wokwi's own exporter, so a
// Chiply design can go to Tiny Tapeout as Verilog (tt_um_<name>.v + cells.v)
// without a Wokwi upload.
//
// Net numbering follows Wokwi: first the Tiny Tapeout blocks in part order
// (input block CLK, RST_N, IN0..7; output block OUT0..7; each bidirectional
// block IN, OUT, OE), then every part in order, pins in definition order;
// VCC/GND symbols always get a numbered constant net. A pin alone on its net
// gets no number (an empty connection). For the reference design the output
// is byte-identical to Wokwi's export.
#include "core/Document.h"
#include "core/Netlist.h"
#include "core/PartLibrary.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace chiply {

struct VerilogOptions {
    std::string moduleName;  // e.g. "tt_um_wokwi_414123795172381697"
    // First line of the file; "" writes a Chiply line naming the source.
    std::string headerComment;
    std::string sourceName;  // used in the default header
};

struct ExportError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Throws ExportError for parts that cannot be exported (unknown types, SR
// flip-flops, which cells.v does not have).
std::string writeVerilog(const Document& doc, const PartLibrary& lib, const VerilogOptions& opt);

// "tt_um_" + the file stem made into a Verilog identifier
// ("My Design.json" -> "tt_um_My_Design").
std::string defaultModuleName(const std::string& fileStem);

// Tiny Tapeout's cells.v (Apache-2.0), the cell library the export uses.
const char* ttCellsV();

// Export as a Tiny Tapeout Verilog project (PLAN.md 5.5): writes
// <dir>/src/<module>.v and <dir>/src/cells.v; if <dir>/info.yaml exists,
// sets language "Verilog", top_module and source_files (and comments out
// wokwi_id); if <dir>/test/Makefile has PROJECT_SOURCES, updates it.
// Returns what it did, one line each. Throws ExportError / std::runtime_error.
std::vector<std::string> exportTtProject(const Document& doc, const PartLibrary& lib, const std::string& dir,
                                         const VerilogOptions& opt);

// info.yaml editing, line by line so comments and layout survive.
std::string patchInfoYaml(const std::string& yaml, const std::string& topModule,
                          const std::vector<std::string>& sources);
// top_module from info.yaml ("" if none).
std::string infoYamlTopModule(const std::string& yaml);

} // namespace chiply
