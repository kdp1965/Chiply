#pragma once
// Custom blocks (PLAN.md 7.2): a Verilog module behind a schematic symbol.
//
// A block is a folder with a block.json and its Verilog:
//
//   blocks/adder4/block.json
//   blocks/adder4/adder4.v
//
//   { "name": "adder4", "module": "adder4", "verilog": ["adder4.v"],
//     "ports": [ { "name": "a", "dir": "in", "width": 4 },
//                { "name": "clk", "dir": "in", "clock": true },
//                { "name": "sum", "dir": "out", "width": 5 } ],
//     "params": { "OFFSET": 0 },
//     "label": "4-bit adder", "prefix": "add" }
//
// It becomes part type "chiply-block-<name>" (an extension part: Extended
// mode only). Multi-bit ports are one pin per bit (a0..a3), so wires stay
// single-bit; the Verilog export joins them back into vectors. Parameters
// come from the part's attrs (defaults from "params").
//
// Blocks are found in a design's own "blocks" folder and in the per-user
// library; the design's folder wins when both have a block of the same name.
#include "core/Document.h"
#include "core/PartLibrary.h"

#include <optional>
#include <string>
#include <vector>

namespace chiply {

// The auto symbol shared by custom blocks and memories: a box with `label`
// at the top, `left` pins (inputs) and `right` pins (outputs, inouts), bit 0
// at the top, all on the 0.1 inch grid, width from the pin names. In px.
struct AutoPin {
    std::string name;
    PinDir dir = PinDir::In;
    bool clock = false;
};
PartDef autoSymbolPart(const std::string& type, const std::string& label, const std::vector<AutoPin>& left,
                       const std::vector<AutoPin>& right);

// Reads <folder>/block.json. On failure returns nullopt and says why.
std::optional<BlockInfo> loadBlock(const std::string& folder, std::string* error);

// The part definition of a block: auto symbol, pins on the 0.1 inch grid
// (inputs left, outputs and inouts right, bit 0 at the top), in px.
PartDef blockPartDef(const BlockInfo& block);

struct BlockScan {
    std::vector<std::string> loaded;   // block names registered
    std::vector<std::string> warnings; // folders that could not be loaded
};

// Loads every block folder under `roots` (in priority order: a block name
// found in an earlier root hides the same name in later ones) into `lib`.
BlockScan scanBlocks(const std::vector<std::string>& roots, PartLibrary& lib = PartLibrary::global());

// $CHIPLY_BLOCKS if set, else ~/Library/Application Support/Chiply/blocks
// on macOS, $XDG_DATA_HOME (or ~/.local/share)/chiply/blocks elsewhere.
std::string defaultUserBlocksDir();

// Where blocks for a design are looked up: <design folder>/blocks (when the
// design has a path), then the per-user library.
std::vector<std::string> blockRoots(const std::string& designPath);

// Verilog files of the blocks a document uses (each once, in part order).
std::vector<std::string> blockSources(const Document& doc, const PartLibrary& lib = PartLibrary::builtin());

} // namespace chiply
