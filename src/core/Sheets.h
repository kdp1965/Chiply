#pragma once
// Sub-sheets (PLAN.md 7.4): a block whose inside is another Chiply
// schematic. Extension parts (Extended mode).
//
// A sheet is an ordinary diagram file in a "sheets" folder next to the
// design (or in the per-user library); its name is the file name
// ("sheets/fulladd.json" -> sheet "fulladd", part type
// "chiply-sheet-fulladd"). Its ports are the port parts placed in it:
//
//   chiply-port-in   "Sheet input":  pin P drives the net inside the sheet
//   chiply-port-out  "Sheet output": pin P reads the net inside the sheet
//
// A port's name is the port part's id. On the sheet's symbol the inputs are
// on the left and the outputs on the right, each ordered top to bottom (then
// left to right) as the port parts are placed in the sheet.
//
// Simulation and the Verilog export work on the flattened design
// (flattenSheets): every instance is replaced by the sheet's chip parts with
// ids "<instance>__<id>", and each port becomes a junction
// "<instance>__<port>" joining the nets outside and inside. Parts of a sheet
// that only serve to try it out on its own (switches, buttons, clock
// generators, LEDs, displays, resistors, Tiny Tapeout blocks, logic
// analyzers, text) are left out of instances. Sheets can contain sheets; a
// sheet that contains itself is an error.
#include "core/Document.h"
#include "core/PartLibrary.h"

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace chiply {

inline bool isPortType(const std::string& type) { return type == "chiply-port-in" || type == "chiply-port-out"; }

struct SheetPort {
    std::string name;
    bool input = true;
};

struct SheetInfo {
    std::string name;
    std::string path;   // the diagram file (absolute)
    Document doc;
    std::vector<SheetPort> ports; // inputs then outputs, each in symbol order
};

// Reads a sheet from a diagram file. On failure returns nullopt and says why
// (unreadable file, a name that is not a Verilog identifier, no ports).
std::optional<SheetInfo> loadSheet(const std::string& path, std::string* error);

// The part definition of a sheet: auto symbol, pins named after the ports.
PartDef sheetPartDef(const SheetInfo& sheet);

struct SheetScan {
    std::vector<std::string> loaded;
    std::vector<std::string> warnings;
};
// Registers every sheet (*.json, sidecar files excepted) in `roots`, in
// priority order: a name found in an earlier root hides later ones.
SheetScan scanSheets(const std::vector<std::string>& roots, PartLibrary& lib = PartLibrary::global());

// $CHIPLY_SHEETS if set, else ~/Library/Application Support/Chiply/sheets
// on macOS, $XDG_DATA_HOME (or ~/.local/share)/chiply/sheets elsewhere.
std::string defaultUserSheetsDir();
// <design folder>/sheets (when the design has a path), then the user library.
std::vector<std::string> sheetRoots(const std::string& designPath);

bool usesSheets(const Document& doc, const PartLibrary& lib = PartLibrary::builtin());

struct FlattenError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct FlatInfo {
    // "instance:port" (top-level instances) -> the pin that stands for it in
    // the flat design.
    std::map<std::string, PinRef> pins;
    // The flat pin for a pin of the original design (unchanged unless it is
    // a sheet instance's port).
    PinRef resolve(const PinRef& ref) const
    {
        auto it = pins.find(ref.str());
        return it == pins.end() ? ref : it->second;
    }
};

// The design with every sheet instance expanded (recursively). A design
// without sheets is returned unchanged. Throws FlattenError when a sheet
// contains itself.
Document flattenSheets(const Document& doc, const PartLibrary& lib = PartLibrary::builtin(), FlatInfo* info = nullptr);

// What stops a sheet from being used: it contains itself, or a part type
// that is not known (also inside the sheets it contains). "" if fine.
std::string sheetProblem(const SheetInfo& sheet, const PartLibrary& lib = PartLibrary::builtin());

} // namespace chiply
