#pragma once
// Design rule checks (PLAN.md 5.2), incremental.
//
// The engine keeps the last document it checked and its results. update()
// compares a new document with that one and re-checks only what an edit
// can have changed:
//   touched parts  = parts added, removed or changed (type, attrs, position,
//                    rotation) and both ends of every wire added or removed;
//   dirty parts    = touched parts plus every part on a net that a touched
//                    part's pin belonged to before or belongs to after;
//   dirty nets     = the nets of dirty parts.
// Violations owned by a dirty part are dropped and the per-part, per-net and
// loop checks run again for the dirty set only; everything else is kept.
// A wire route or colour change touches nothing. runFull() checks
// everything from scratch; for any document both give the same result.
//
// Scope: the chip. Checks look at logic cells, VCC/GND and the design side
// of the Tiny Tapeout blocks; the board side (EXT* pins, switches, LEDs,
// displays, clock generators, logic analyzers) is the testbench.
#include "core/Document.h"
#include "core/Netlist.h"
#include "core/PartLibrary.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace chiply::drc {

enum class Severity { Error, Warning, Info };
const char* severityName(Severity s);

struct CheckInfo {
    std::string id;
    std::string title;
    Severity severity;
    bool defaultOn;
    std::string description;
};
// Every check, in display order.
const std::vector<CheckInfo>& checks();
const CheckInfo* findCheck(const std::string& id);

struct Violation {
    std::string check;
    Severity severity = Severity::Warning;
    std::string message;
    std::vector<std::string> parts; // involved parts: what to show and select (and the owners)
    std::vector<PinRef> pins;       // pins to ring; for net checks every pin of the net
    std::string key;                // stable identity (check + subject) for waivers and the pane
};

struct Stats {
    bool full = false;
    int partsChecked = 0;
    int netsChecked = 0;
    int partsTotal = 0;
    double ms = 0;
};

class Engine {
public:
    explicit Engine(const PartLibrary& lib = PartLibrary::builtin());

    // Extended mode (PLAN.md 7.1): extension parts are fine. In Wokwi mode
    // the extension-part check lists them. Changing it re-checks everything.
    void setExtensionsAllowed(bool on);
    // Folder that relative ROM "file" attrs are read from. Re-checks everything.
    void setBaseDir(const std::string& dir);
    bool extensionsAllowed() const { return m_extensionsAllowed; }

    // Which checks run. Turning a check on re-checks the whole design for it.
    bool enabled(const std::string& check) const { return m_enabled.count(check) > 0; }
    void setEnabled(const std::string& check, bool on);
    const std::set<std::string>& enabledChecks() const { return m_enabled; }

    const Stats& runFull(const Document& doc);
    // Incremental; the first call (or after clear()) is a full pass.
    const Stats& update(const Document& doc);
    void clear();
    // Every check back to its default on/off state (and clear()).
    void resetChecks();
    const Stats& lastStats() const { return m_stats; }

    // Current results of the enabled checks, errors first, then by check and
    // location.
    std::vector<Violation> violations() const;
    std::size_t count(Severity s) const;

private:
    // `owners`: the parts whose re-check reproduces the violation (default:
    // all of v.parts). A violation is dropped when any owner becomes dirty,
    // so every owner must be one whose check runs again.
    void add(Violation v, std::vector<std::string> owners = {});
    void removeOwnedBy(const std::string& part);
    void erase(const std::string& key);
    void checkPart(int device);
    bool netDriven(int net) const;
    void checkNet(int net);
    void checkLoops(const std::set<int>& devices, bool all);
    void checkWire(const Wire& w);
    void checkStacked(const std::string& partId);
    void checkDuplicateId(const std::string& partId);
    void checkBidirBits();
    bool on(const char* check) const { return m_enabled.count(check) > 0; }

    const PartLibrary& m_lib;
    std::set<std::string> m_enabled;
    bool m_extensionsAllowed = false;
    std::string m_baseDir;
    Document m_doc;  // last checked
    Netlist m_nl;    // its netlist (devices point into m_doc)
    bool m_have = false;
    std::map<std::string, Violation> m_viol;                 // key -> violation
    std::map<std::string, std::vector<std::string>> m_ownersOf; // key -> owners
    std::map<std::string, std::set<std::string>> m_owned;    // part id -> keys
    Stats m_stats;
};

// Part types that become Verilog cells or constants (the chip).
bool isLogicCell(const PartDef* def);
// Gates and muxes: no state, so a cycle through them is a loop.
bool isCombinational(const PartDef* def);
bool isFlipFlop(const PartDef* def);
// A legal Verilog identifier that is not a keyword or a name the export uses.
bool isValidVerilogId(const std::string& id);

} // namespace chiply::drc
