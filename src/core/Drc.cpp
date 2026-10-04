#include "core/Drc.h"

#include "core/Memory.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <regex>
#include <sstream>

namespace chiply::drc {

namespace {

const std::vector<CheckInfo> kChecks = {
    {"multiple-drivers", "Multiple drivers", Severity::Error, true,
     "Two or more outputs (or VCC/GND) drive the same net."},
    {"short-circuit", "Short circuit", Severity::Error, true, "VCC and GND are on the same net."},
    {"combinational-loop", "Combinational loop", Severity::Error, true,
     "A cycle through gates or muxes with no flip-flop in it (a latch or oscillator)."},
    {"dangling-wire", "Dangling wire", Severity::Error, true,
     "A wire names a part or pin that does not exist."},
    {"unknown-part", "Unknown part", Severity::Error, true, "A part type Chiply does not know (it cannot be exported)."},
    {"invalid-id", "Invalid id", Severity::Error, true,
     "A cell id that is not a legal Verilog name, is a keyword or export name, or is used twice."},
    {"tt-bidir-bit", "Bidirectional bit", Severity::Error, true,
     "A Tiny Tapeout bidirectional block with a missing, invalid or repeated verilogBit."},
    {"unconnected-input", "Unconnected input", Severity::Warning, true,
     "An input pin with no wire, or whose net has no driver."},
    {"clock-from-logic", "Clock from logic", Severity::Warning, true,
     "A flip-flop clock driven by a gate instead of a clock input or a flip-flop."},
    {"stacked-parts", "Stacked parts", Severity::Warning, true,
     "Two parts of the same type at the same position (an invisible duplicate)."},
    {"memory-contents", "ROM contents", Severity::Error, true,
     "A ROM whose data or file cannot be read, has more words than the ROM, or words wider than it."},
    {"extension-part", "Chiply extension part", Severity::Warning, true,
     "A Chiply-only part (Wokwi cannot load the design). Reported in Wokwi mode only."},
    {"unconnected-output", "Unconnected output", Severity::Info, false, "An output that drives nothing."},
};

// Devices between nets that DRC looks through (inputs driven via a switch).
bool passesThrough(const std::string& type)
{
    return type == "wokwi-slide-switch" || type == "wokwi-pushbutton" || type == "wokwi-dip-switch-8"
        || type == "wokwi-resistor";
}

bool isExtPin(const std::string& pin) { return pin.rfind("EXT", 0) == 0; }

bool isTtBlock(const std::string& type) { return type.rfind("board-tt-block", 0) == 0; }

// Pins that belong to the chip: logic cells, VCC/GND, and the design side of
// the Tiny Tapeout blocks.
bool chipPin(const Device& d, std::size_t pin)
{
    if (isLogicCell(d.def) || (d.def && (d.def->block || d.def->memory)))
        return true;
    if (d.type == "wokwi-vcc" || d.type == "wokwi-gnd")
        return true;
    if (isTtBlock(d.type))
        return !isExtPin(d.pinNames[pin]) && d.pinNames[pin] != "UIO";
    return false;
}

bool isDriver(PinDir d) { return d == PinDir::Out || d == PinDir::InOut || d == PinDir::Power; }

std::string join(const std::vector<std::string>& v, const char* sep = ", ")
{
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i)
        s += (i ? sep : "") + v[i];
    return s;
}

std::string ref(const Device& d, int pin) { return d.partId + ":" + d.pinNames[size_t(pin)]; }

std::string attrString(const Part& p, const char* key)
{
    if (!p.attrs.is_object() || !p.attrs.contains(key))
        return {};
    const Json& v = p.attrs[key];
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_number_integer())
        return std::to_string(v.get<long long>());
    return v.dump();
}

} // namespace

const char* severityName(Severity s)
{
    switch (s) {
    case Severity::Error: return "error";
    case Severity::Warning: return "warning";
    case Severity::Info: return "info";
    }
    return "?";
}

const std::vector<CheckInfo>& checks() { return kChecks; }

const CheckInfo* findCheck(const std::string& id)
{
    for (const CheckInfo& c : kChecks)
        if (c.id == id)
            return &c;
    return nullptr;
}

bool isLogicCell(const PartDef* def)
{
    return def && def->verilog.is_object() && def->verilog.contains("cell");
}

bool isFlipFlop(const PartDef* def) { return def && def->type.rfind("wokwi-flip-flop", 0) == 0; }

bool isCombinational(const PartDef* def)
{
    return isLogicCell(def) && !isFlipFlop(def);
}

bool isValidVerilogId(const std::string& id)
{
    static const std::regex ident("[A-Za-z_][A-Za-z0-9_$]*");
    static const std::regex netN("net[0-9]+");
    static const std::set<std::string> reserved = {
        // Verilog-2005 and SystemVerilog keywords most likely to be typed
        "always", "and", "assign", "automatic", "begin", "buf", "bufif0", "bufif1", "case", "casex", "casez",
        "cell", "cmos", "config", "deassign", "default", "defparam", "design", "disable", "edge", "else", "end",
        "endcase", "endconfig", "endfunction", "endgenerate", "endmodule", "endprimitive", "endspecify",
        "endtable", "endtask", "event", "for", "force", "forever", "fork", "function", "generate", "genvar",
        "highz0", "highz1", "if", "ifnone", "incdir", "include", "initial", "inout", "input", "instance",
        "integer", "join", "large", "liblist", "library", "localparam", "macromodule", "medium", "module",
        "nand", "negedge", "nmos", "nor", "noshowcancelled", "not", "notif0", "notif1", "or", "output",
        "parameter", "pmos", "posedge", "primitive", "pull0", "pull1", "pulldown", "pullup",
        "pulsestyle_ondetect", "pulsestyle_onevent", "rcmos", "real", "realtime", "reg", "release", "repeat",
        "rnmos", "rpmos", "rtran", "rtranif0", "rtranif1", "scalared", "showcancelled", "signed", "small",
        "specify", "specparam", "strong0", "strong1", "supply0", "supply1", "table", "task", "time", "tran",
        "tranif0", "tranif1", "tri", "tri0", "tri1", "triand", "trior", "trireg", "unsigned", "use", "uwire",
        "vectored", "wait", "wand", "weak0", "weak1", "while", "wire", "wor", "xnor", "xor", "logic", "bit",
        "byte", "int", "interface", "class", "package",
        // names the export itself uses
        "ui_in", "uo_out", "uio_in", "uio_out", "uio_oe", "ena", "clk", "rst_n"};
    return std::regex_match(id, ident) && !reserved.count(id) && !std::regex_match(id, netN);
}

Engine::Engine(const PartLibrary& lib)
    : m_lib(lib)
{
    for (const CheckInfo& c : kChecks)
        if (c.defaultOn)
            m_enabled.insert(c.id);
}

void Engine::setEnabled(const std::string& check, bool on)
{
    if (!findCheck(check) || on == enabled(check))
        return;
    if (on) {
        m_enabled.insert(check);
        if (m_have) {
            const Document doc = m_doc;
            runFull(doc); // results of a check that was off are not kept
        }
    } else {
        m_enabled.erase(check);
        std::vector<std::string> drop;
        for (const auto& [k, v] : m_viol)
            if (v.check == check)
                drop.push_back(k);
        for (const std::string& k : drop)
            erase(k);
    }
}

void Engine::clear()
{
    m_viol.clear();
    m_owned.clear();
    m_ownersOf.clear();
    m_have = false;
    m_doc = Document();
    m_nl = Netlist();
}

void Engine::setExtensionsAllowed(bool on)
{
    if (on == m_extensionsAllowed)
        return;
    m_extensionsAllowed = on;
    if (m_have) {
        const Document doc = m_doc;
        runFull(doc);
    }
}

void Engine::setBaseDir(const std::string& dir)
{
    if (dir == m_baseDir)
        return;
    m_baseDir = dir;
    if (m_have) {
        const Document doc = m_doc;
        runFull(doc);
    }
}

void Engine::resetChecks()
{
    clear();
    m_enabled.clear();
    for (const CheckInfo& c : kChecks)
        if (c.defaultOn)
            m_enabled.insert(c.id);
}

void Engine::add(Violation v, std::vector<std::string> owners)
{
    if (!on(v.check.c_str()))
        return;
    if (const CheckInfo* c = findCheck(v.check))
        v.severity = c->severity;
    if (owners.empty())
        owners = v.parts;
    auto old = m_ownersOf.find(v.key);
    if (old != m_ownersOf.end())
        for (const std::string& p : old->second)
            m_owned[p].erase(v.key);
    for (const std::string& p : owners)
        m_owned[p].insert(v.key);
    m_ownersOf[v.key] = std::move(owners);
    m_viol[v.key] = std::move(v);
}

void Engine::erase(const std::string& key)
{
    auto o = m_ownersOf.find(key);
    if (o != m_ownersOf.end()) {
        for (const std::string& p : o->second)
            m_owned[p].erase(key);
        m_ownersOf.erase(o);
    }
    m_viol.erase(key);
}

void Engine::removeOwnedBy(const std::string& part)
{
    auto it = m_owned.find(part);
    if (it == m_owned.end())
        return;
    const std::set<std::string> keys = it->second;
    for (const std::string& k : keys)
        erase(k);
}

std::vector<Violation> Engine::violations() const
{
    std::vector<Violation> out;
    for (const auto& [k, v] : m_viol)
        if (enabled(v.check))
            out.push_back(v);
    auto order = [](const std::string& check) {
        for (std::size_t i = 0; i < kChecks.size(); ++i)
            if (kChecks[i].id == check)
                return int(i);
        return 999;
    };
    std::stable_sort(out.begin(), out.end(), [&](const Violation& a, const Violation& b) {
        if (a.severity != b.severity)
            return a.severity < b.severity;
        if (a.check != b.check)
            return order(a.check) < order(b.check);
        return a.key < b.key;
    });
    return out;
}

std::size_t Engine::count(Severity s) const
{
    std::size_t n = 0;
    for (const auto& [k, v] : m_viol)
        n += (v.severity == s && enabled(v.check));
    return n;
}

// ---- checks ----

bool Engine::netDriven(int net0) const
{
    // Any driver counts (a clock generator feeding a gate is fine on Wokwi),
    // also through switches, buttons and resistors (passive devices between
    // nets), e.g. a gate input on a slide switch's middle pin.
    std::set<int> seen{net0};
    std::vector<int> work{net0};
    while (!work.empty()) {
        const int net = work.back();
        work.pop_back();
        const Net& n = m_nl.nets[size_t(net)];
        if (!n.drivers.empty())
            return true;
        for (const NetPin& np : n.pins) {
            const Device& d = m_nl.devices[size_t(np.device)];
            if (!passesThrough(d.type))
                continue;
            for (int other : d.pinNets)
                if (seen.insert(other).second)
                    work.push_back(other);
        }
    }
    return false;
}

void Engine::checkPart(int di)
{
    const Device& d = m_nl.devices[size_t(di)];
    const Part* part = m_doc.findPart(d.partId);
    if (!d.def) {
        if (!part || part->type == "wokwi-text")
            return;
        add({"unknown-part", {}, "Unknown part type \"" + d.type + "\" (" + d.partId + ")", {d.partId}, {},
             "unknown-part:" + d.partId});
        return;
    }
    if (!m_extensionsAllowed && isExtensionType(d.type))
        add({"extension-part", {}, d.partId + " (" + d.def->label + ") is a Chiply extension: Wokwi cannot load this design",
             {d.partId}, {}, "extension-part:" + d.partId});
    if (d.def->memory && d.def->memory->rom && part) {
        std::string err;
        memoryContents(*part, *d.def->memory, m_baseDir, &err);
        if (!err.empty())
            add({"memory-contents", {}, d.partId + ": " + err, {d.partId}, {}, "memory-contents:" + d.partId});
    }
    if ((isLogicCell(d.def) || d.def->memory) && !isValidVerilogId(d.partId))
        add({"invalid-id", {}, "\"" + d.partId + "\" is not usable as a Verilog instance name", {d.partId}, {},
             "invalid-id:" + d.partId});
    for (std::size_t i = 0; i < d.pinNames.size(); ++i) {
        if (!chipPin(d, i) || !d.def->findPin(d.pinNames[i]))
            continue;
        const int net = d.pinNets[i];
        const Net& n = m_nl.nets[size_t(net)];
        const PinRef pr{d.partId, d.pinNames[i]};
        if (d.pinDirs[i] == PinDir::In) {
            if (n.pins.size() < 2) {
                add({"unconnected-input", {}, pr.str() + " is not connected", {d.partId}, {pr},
                     "unconnected-input:" + pr.str()});
            } else {
                if (!netDriven(net)) {
                    std::vector<std::string> others;
                    std::vector<std::string> owners{d.partId};
                    for (const NetPin& np : n.pins)
                        if (np.device != di || size_t(np.pin) != i) {
                            others.push_back(ref(m_nl.devices[size_t(np.device)], np.pin));
                            owners.push_back(m_nl.devices[size_t(np.device)].partId);
                        }
                    std::sort(owners.begin(), owners.end());
                    owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
                    if (others.size() > 6) {
                        others.resize(6);
                        others.push_back("...");
                    }
                    add({"unconnected-input", {}, pr.str() + " has no driver (net with " + join(others) + ")",
                         owners, {pr}, "unconnected-input:" + pr.str()},
                        {d.partId});
                }
            }
        } else if (d.pinDirs[i] == PinDir::Out && isLogicCell(d.def)) {
            if (n.loads.empty())
                add({"unconnected-output", {}, pr.str() + " drives nothing", {d.partId}, {pr},
                     "unconnected-output:" + pr.str()});
        }
    }
    // Flip-flop clock from combinational logic.
    if (isFlipFlop(d.def)) {
        const int net = m_nl.netOf({d.partId, "CLK"});
        if (net >= 0)
            for (const NetPin& np : m_nl.nets[size_t(net)].drivers) {
                const Device& src = m_nl.devices[size_t(np.device)];
                if (isCombinational(src.def))
                    add({"clock-from-logic", {}, d.partId + ":CLK is driven by " + ref(src, np.pin) + " (" + src.def->label + ")",
                         {d.partId, src.partId}, {{d.partId, "CLK"}, {src.partId, src.pinNames[size_t(np.pin)]}},
                         "clock-from-logic:" + d.partId},
                        {d.partId});
            }
    }
}

void Engine::checkNet(int ni)
{
    const Net& n = m_nl.nets[size_t(ni)];
    std::vector<std::string> drivers, owners;
    std::vector<PinRef> pins;
    bool vcc = false, gnd = false;
    for (const NetPin& np : n.pins) {
        const Device& d = m_nl.devices[size_t(np.device)];
        if (!chipPin(d, size_t(np.pin)))
            continue;
        pins.push_back({d.partId, d.pinNames[size_t(np.pin)]});
        if (isDriver(d.pinDirs[size_t(np.pin)])) {
            drivers.push_back(ref(d, np.pin));
            owners.push_back(d.partId);
        }
        vcc |= d.type == "wokwi-vcc";
        gnd |= d.type == "wokwi-gnd";
    }
    std::sort(drivers.begin(), drivers.end());
    std::sort(owners.begin(), owners.end());
    owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
    if (vcc && gnd) {
        add({"short-circuit", {}, "VCC and GND are connected (" + join(drivers) + ")", owners, pins,
             "short-circuit:" + join(drivers, ",")});
    } else if (owners.size() >= 2) {
        // Several VCC (or GND) symbols on one net are fine: same constant.
        bool sameConstant = true;
        std::string type;
        for (const std::string& o : owners) {
            const Device& d = m_nl.devices[size_t(m_nl.deviceOf(o))];
            if (d.type != "wokwi-vcc" && d.type != "wokwi-gnd")
                sameConstant = false;
            else if (type.empty())
                type = d.type;
            else if (type != d.type)
                sameConstant = false;
        }
        if (!sameConstant)
            add({"multiple-drivers", {}, "Net driven by " + std::to_string(drivers.size()) + " outputs: " + join(drivers),
                 owners, pins, "multiple-drivers:" + join(drivers, ",")});
    }
}

void Engine::checkLoops(const std::set<int>& devices, bool all)
{
    if (!on("combinational-loop"))
        return;
    // Graph over combinational cells: an edge from a cell to each cell that
    // reads its output net.
    const std::size_t n = m_nl.devices.size();
    auto succ = [&](int di, std::vector<int>& out) {
        out.clear();
        const Device& d = m_nl.devices[size_t(di)];
        for (std::size_t i = 0; i < d.pinNames.size(); ++i)
            if (d.pinDirs[i] == PinDir::Out)
                for (const NetPin& np : m_nl.nets[size_t(d.pinNets[i])].loads)
                    if (isCombinational(m_nl.devices[size_t(np.device)].def))
                        out.push_back(np.device);
    };
    auto report = [&](std::vector<int> scc, bool selfLoop) {
        if (scc.size() < 2 && !selfLoop)
            return;
        std::vector<std::string> ids;
        for (int di : scc)
            ids.push_back(m_nl.devices[size_t(di)].partId);
        std::sort(ids.begin(), ids.end());
        std::string list = join(std::vector<std::string>(ids.begin(), ids.begin() + std::ptrdiff_t(std::min<std::size_t>(ids.size(), 8))));
        if (ids.size() > 8)
            list += ", ... (" + std::to_string(ids.size()) + " cells)";
        // Loops are disjoint: one that shares a cell with this one is an
        // older, smaller loop this one has grown out of.
        for (const std::string& id : ids) {
            auto it = m_owned.find(id);
            if (it == m_owned.end())
                continue;
            std::vector<std::string> stale;
            for (const std::string& k : it->second)
                if (k.rfind("combinational-loop:", 0) == 0)
                    stale.push_back(k);
            for (const std::string& k : stale)
                erase(k);
        }
        add({"combinational-loop", {},
             (ids.size() == 1 ? ids[0] + " feeds its own input" : "Loop through " + list), ids, {},
             "combinational-loop:" + join(ids, ",")});
    };
    std::vector<int> tmp;
    if (all || devices.size() > 64) {
        // Tarjan over the whole graph (iterative).
        std::vector<int> index(n, -1), low(n, 0), stack;
        std::vector<char> onStack(n, 0);
        int counter = 0;
        for (std::size_t root = 0; root < n; ++root) {
            if (!isCombinational(m_nl.devices[root].def) || index[root] >= 0)
                continue;
            struct Frame {
                int v;
                std::vector<int> next;
                std::size_t i;
            };
            std::vector<Frame> call;
            auto push = [&](int v) {
                index[size_t(v)] = low[size_t(v)] = counter++;
                stack.push_back(v);
                onStack[size_t(v)] = 1;
                Frame f{v, {}, 0};
                succ(v, f.next);
                call.push_back(std::move(f));
            };
            push(int(root));
            while (!call.empty()) {
                Frame& f = call.back();
                if (f.i < f.next.size()) {
                    const int w = f.next[f.i++];
                    if (index[size_t(w)] < 0)
                        push(w);
                    else if (onStack[size_t(w)])
                        low[size_t(f.v)] = std::min(low[size_t(f.v)], index[size_t(w)]);
                    continue;
                }
                const int v = f.v;
                if (low[size_t(v)] == index[size_t(v)]) {
                    std::vector<int> scc;
                    int w;
                    do {
                        w = stack.back();
                        stack.pop_back();
                        onStack[size_t(w)] = 0;
                        scc.push_back(w);
                    } while (w != v);
                    bool self = false;
                    if (scc.size() == 1) {
                        succ(v, tmp);
                        self = std::find(tmp.begin(), tmp.end(), v) != tmp.end();
                    }
                    bool relevant = all;
                    for (int x : scc)
                        relevant |= devices.count(x) > 0;
                    if (relevant)
                        report(scc, self);
                }
                call.pop_back();
                if (!call.empty())
                    low[size_t(call.back().v)] = std::min(low[size_t(call.back().v)], low[size_t(v)]);
            }
        }
        return;
    }
    // Few dirty cells: the loop through a cell is the set of cells both
    // reachable from it and reaching it.
    std::vector<std::vector<int>> pred;
    bool predBuilt = false;
    std::set<int> done;
    for (int d0 : devices) {
        if (done.count(d0) || !isCombinational(m_nl.devices[size_t(d0)].def))
            continue;
        std::vector<char> fwd(n, 0), bwd(n, 0);
        std::vector<int> work{d0};
        bool self = false;
        while (!work.empty()) {
            const int v = work.back();
            work.pop_back();
            succ(v, tmp);
            for (int w : tmp) {
                if (w == d0)
                    self = true;
                if (!fwd[size_t(w)]) {
                    fwd[size_t(w)] = 1;
                    work.push_back(w);
                }
            }
        }
        if (!self)
            continue; // d0 is on no cycle
        if (!predBuilt) {
            pred.assign(n, {});
            for (std::size_t v = 0; v < n; ++v)
                if (isCombinational(m_nl.devices[v].def)) {
                    succ(int(v), tmp);
                    for (int w : tmp)
                        pred[size_t(w)].push_back(int(v));
                }
            predBuilt = true;
        }
        work = {d0};
        bwd[size_t(d0)] = 1;
        while (!work.empty()) {
            const int v = work.back();
            work.pop_back();
            for (int w : pred[size_t(v)])
                if (!bwd[size_t(w)]) {
                    bwd[size_t(w)] = 1;
                    work.push_back(w);
                }
        }
        std::vector<int> scc;
        for (std::size_t v = 0; v < n; ++v)
            if ((fwd[v] && bwd[v]) || int(v) == d0)
                scc.push_back(int(v));
        for (int v : scc)
            done.insert(v);
        report(scc, scc.size() == 1);
    }
}

void Engine::checkWire(const Wire& w)
{
    for (const PinRef* end : {&w.from, &w.to}) {
        const Part* p = m_doc.findPart(end->part);
        std::string problem;
        if (!p)
            problem = "names part \"" + end->part + "\", which does not exist";
        else if (const PartDef* def = m_lib.find(p->type); def && !def->findPin(end->pin))
            problem = "names pin \"" + end->pin + "\", which " + p->type + " does not have";
        if (problem.empty())
            continue;
        std::vector<std::string> owners{end->part};
        const PinRef& other = (end == &w.from) ? w.to : w.from;
        if (other.part != end->part)
            owners.push_back(other.part);
        add({"dangling-wire", {}, "Wire " + w.from.str() + " - " + w.to.str() + " " + problem, owners,
             {other}, "dangling-wire:" + w.from.str() + "|" + w.to.str() + "|" + end->str()});
    }
}

void Engine::checkStacked(const std::string& id)
{
    for (const Part& pp : m_doc.parts) { // every part with this id (ids may repeat)
        const Part* p = &pp;
        if (p->id != id || p->type == "wokwi-text")
            continue;
        for (const Part& q : m_doc.parts)
            if (&q != p && q.type == p->type && std::abs(q.left - p->left) < 0.01 && std::abs(q.top - p->top) < 0.01) {
                const std::string a = std::min(p->id, q.id), b = std::max(p->id, q.id);
                add({"stacked-parts", {}, a + " and " + b + " are stacked at the same position", {a, b}, {},
                     "stacked-parts:" + a + "," + b});
            }
    }
}

void Engine::checkDuplicateId(const std::string& id)
{
    int n = 0;
    for (const Part& q : m_doc.parts)
        n += q.id == id;
    if (n > 1)
        add({"invalid-id", {}, "\"" + id + "\" is used by " + std::to_string(n) + " parts", {id}, {},
             "invalid-id-duplicate:" + id});
}

void Engine::checkBidirBits()
{
    std::map<int, std::vector<std::string>> byBit;
    std::vector<std::string> all;
    for (const Part& p : m_doc.parts) {
        if (p.type != "board-tt-block-bidirectional-io")
            continue;
        all.push_back(p.id);
        const std::string s = attrString(p, "verilogBit");
        int bit = -1;
        try {
            std::size_t used = 0;
            bit = s.empty() ? -1 : std::stoi(s, &used);
            if (used != s.size())
                bit = -1;
        } catch (...) {
        }
        if (bit < 0 || bit > 7)
            add({"tt-bidir-bit", {}, p.id + " has " + (s.empty() ? std::string("no verilogBit") : "verilogBit \"" + s + "\" (not 0..7)"),
                 {p.id}, {}, "tt-bidir-bit:" + p.id});
        else
            byBit[bit].push_back(p.id);
    }
    for (auto& [bit, ids] : byBit)
        if (ids.size() > 1) {
            std::sort(ids.begin(), ids.end());
            add({"tt-bidir-bit", {}, "verilogBit " + std::to_string(bit) + " is used by " + join(ids), ids, {},
                 "tt-bidir-bit:bit" + std::to_string(bit)});
        }
}

// ---- passes ----

const Stats& Engine::runFull(const Document& doc)
{
    const auto t0 = std::chrono::steady_clock::now();
    m_viol.clear();
    m_owned.clear();
    m_ownersOf.clear();
    m_doc = doc;
    m_nl = Netlist::build(m_doc, m_lib);
    m_have = true;
    std::set<std::string> ids;
    for (std::size_t i = 0; i < m_nl.devices.size(); ++i)
        checkPart(int(i));
    for (std::size_t i = 0; i < m_nl.nets.size(); ++i)
        checkNet(int(i));
    checkLoops({}, true);
    for (const Wire& w : m_doc.wires)
        checkWire(w);
    for (const Part& p : m_doc.parts) {
        if (ids.insert(p.id).second) {
            checkStacked(p.id);
            checkDuplicateId(p.id);
        }
    }
    checkBidirBits();
    m_stats = {true, int(m_nl.devices.size()), int(m_nl.nets.size()), int(m_nl.devices.size()),
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()};
    return m_stats;
}

const Stats& Engine::update(const Document& doc)
{
    if (!m_have)
        return runFull(doc);
    const auto t0 = std::chrono::steady_clock::now();

    // Touched parts.
    std::set<std::string> touched;
    std::map<std::string, const Part*> oldParts, newParts;
    for (const Part& p : m_doc.parts)
        oldParts.emplace(p.id, &p);
    for (const Part& p : doc.parts)
        newParts.emplace(p.id, &p);
    auto samePart = [](const Part& a, const Part& b) {
        return a.type == b.type && a.left == b.left && a.top == b.top && a.rotate == b.rotate && a.attrs == b.attrs;
    };
    for (const auto& [id, p] : oldParts) {
        auto it = newParts.find(id);
        if (it == newParts.end() || !samePart(*p, *it->second))
            touched.insert(id);
    }
    for (const auto& [id, p] : newParts)
        if (!oldParts.count(id))
            touched.insert(id);
    if (oldParts.size() != m_doc.parts.size() || newParts.size() != doc.parts.size()) {
        // Duplicate ids: treat every duplicated id as touched.
        std::map<std::string, int> n;
        for (const Part& p : m_doc.parts)
            ++n[p.id];
        for (const Part& p : doc.parts)
            ++n[p.id];
        for (const auto& [id, c] : n)
            if (c > 2 || (c == 2 && !(oldParts.count(id) && newParts.count(id))))
                touched.insert(id);
        for (const Part& p : m_doc.parts)
            if (std::count_if(m_doc.parts.begin(), m_doc.parts.end(), [&](const Part& q) { return q.id == p.id; }) > 1)
                touched.insert(p.id);
        for (const Part& p : doc.parts)
            if (std::count_if(doc.parts.begin(), doc.parts.end(), [&](const Part& q) { return q.id == p.id; }) > 1)
                touched.insert(p.id);
    }
    // Wires added or removed (by their ends; route and colour do not matter).
    std::multiset<std::pair<std::string, std::string>> oldW, newW;
    for (const Wire& w : m_doc.wires)
        oldW.insert({w.from.str(), w.to.str()});
    for (const Wire& w : doc.wires)
        newW.insert({w.from.str(), w.to.str()});
    std::vector<std::pair<std::string, std::string>> diff;
    std::set_symmetric_difference(oldW.begin(), oldW.end(), newW.begin(), newW.end(), std::back_inserter(diff));
    for (const auto& [a, b] : diff) {
        if (auto r = PinRef::parse(a))
            touched.insert(r->part);
        if (auto r = PinRef::parse(b))
            touched.insert(r->part);
    }

    m_stats = {false, 0, 0, int(doc.parts.size()), 0};
    if (touched.empty()) {
        m_doc = doc; // positions of untouched parts are identical; keep the copy current
        m_nl = Netlist::build(m_doc, m_lib);
        m_stats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return m_stats;
    }

    // Dirty parts: touched + everything sharing a net with a touched part,
    // before and after the edit.
    std::set<std::string> dirty = touched;
    auto spread = [&](const Netlist& nl) {
        // Nets of touched parts, extended through switches and resistors
        // (whether an input is driven looks through them).
        std::set<int> nets;
        std::vector<int> work;
        for (std::size_t di = 0; di < nl.devices.size(); ++di) // every device of a touched id (ids may repeat)
            if (touched.count(nl.devices[di].partId))
                for (int net : nl.devices[di].pinNets)
                    if (nets.insert(net).second)
                        work.push_back(net);
        while (!work.empty()) {
            const int net = work.back();
            work.pop_back();
            for (const NetPin& np : nl.nets[size_t(net)].pins) {
                const Device& d = nl.devices[size_t(np.device)];
                dirty.insert(d.partId);
                if (passesThrough(d.type))
                    for (int other : d.pinNets)
                        if (nets.insert(other).second)
                            work.push_back(other);
            }
        }
    };
    spread(m_nl);
    bool bidirTouched = std::any_of(touched.begin(), touched.end(), [&](const std::string& id) {
        auto o = oldParts.find(id);
        auto n = newParts.find(id);
        return (o != oldParts.end() && o->second->type == "board-tt-block-bidirectional-io")
            || (n != newParts.end() && n->second->type == "board-tt-block-bidirectional-io");
    });

    m_doc = doc;
    m_nl = Netlist::build(m_doc, m_lib);
    spread(m_nl);

    for (const std::string& id : dirty) {
        auto o = oldParts.find(id);
        auto n = newParts.find(id);
        bidirTouched |= (o != oldParts.end() && o->second->type == "board-tt-block-bidirectional-io")
            || (n != newParts.end() && n->second->type == "board-tt-block-bidirectional-io");
    }
    // A loop that loses a member may leave smaller loops made only of
    // untouched cells: re-check every member of a dropped loop.
    std::set<std::string> loopMembers;
    for (const std::string& id : dirty) {
        auto it = m_owned.find(id);
        if (it == m_owned.end())
            continue;
        for (const std::string& k : it->second)
            if (k.rfind("combinational-loop:", 0) == 0)
                for (const std::string& o : m_ownersOf[k])
                    loopMembers.insert(o);
    }
    for (const std::string& id : dirty)
        removeOwnedBy(id);
    if (bidirTouched) {
        // The bit check looks at all blocks together: redo it as a whole.
        std::vector<std::string> drop;
        for (const auto& [k, v] : m_viol)
            if (v.check == "tt-bidir-bit")
                drop.push_back(k);
        for (const std::string& k : drop)
            erase(k);
    }

    std::set<int> dirtyDevices, dirtyNets;
    for (std::size_t di = 0; di < m_nl.devices.size(); ++di) {
        if (!dirty.count(m_nl.devices[di].partId))
            continue;
        dirtyDevices.insert(int(di));
        for (int net : m_nl.devices[di].pinNets)
            dirtyNets.insert(net);
    }
    for (int di : dirtyDevices)
        checkPart(di);
    for (int net : dirtyNets)
        checkNet(net);
    std::set<int> loopDevices = dirtyDevices;
    if (!loopMembers.empty())
        for (std::size_t di = 0; di < m_nl.devices.size(); ++di)
            if (loopMembers.count(m_nl.devices[di].partId))
                loopDevices.insert(int(di));
    checkLoops(loopDevices, false);
    for (const Wire& w : m_doc.wires)
        if (dirty.count(w.from.part) || dirty.count(w.to.part))
            checkWire(w);
    for (const std::string& id : dirty) {
        checkStacked(id);
        checkDuplicateId(id);
    }
    if (bidirTouched)
        checkBidirBits();
    m_stats.partsChecked = int(dirtyDevices.size());
    m_stats.netsChecked = int(dirtyNets.size());
    m_stats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return m_stats;
}

} // namespace chiply::drc
