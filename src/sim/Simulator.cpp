#include "sim/Simulator.h"

#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
#include <set>

namespace chiply::sim {

namespace {

bool isFlop(int k)
{
    return k >= 9 && k <= 12; // Dff, Dffr, Dffsr, Srff (see Simulator::Kind)
}

bool rising(V prev, V cur)
{
    // Verilog posedge: 0->1, 0->x, x->1 (z counts as x).
    prev = in(prev), cur = in(cur);
    return (prev == V::L && cur != V::L) || (prev == V::X && cur == V::H);
}

// Parts with no simulation behaviour at all (no warning).
const std::set<std::string> kInert = {"wokwi-text", "wokwi-junction", "wokwi-led", "wokwi-7segment",
                                      "wokwi-logic-analyzer", "wokwi-pi-pico"};

} // namespace

std::optional<double> parseFrequency(const std::string& s)
{
    std::size_t i = 0;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.'))
        ++i;
    if (i == 0)
        return std::nullopt;
    double v;
    try {
        v = std::stod(s.substr(0, i));
    } catch (...) {
        return std::nullopt;
    }
    std::string unit = s.substr(i);
    while (!unit.empty() && unit.front() == ' ')
        unit.erase(unit.begin());
    if (!unit.empty() && (unit.front() == 'k' || unit.front() == 'K'))
        v *= 1e3;
    else if (!unit.empty() && unit.front() == 'M')
        v *= 1e6;
    else if (!unit.empty() && unit.front() != 'H' && unit.front() != 'h')
        return std::nullopt;
    return v > 0 ? std::optional<double>(v) : std::nullopt;
}

Simulator::Simulator(const Netlist& nl, Options opt)
    : m_nl(nl)
    , m_opt(opt)
{
    const std::size_t nets = nl.nets.size();
    m_netSlots.resize(nets);
    m_fanout.resize(nets);
    m_groupOf.resize(nets);
    m_groupNets.resize(nets);
    m_groupValue.assign(nets, V::Z);
    m_groupStrength.assign(nets, Strength::None);
    m_groupDirty.assign(nets, 0);
    m_extSlot.assign(nets, -1);

    static const std::map<std::string, Kind> kinds = {
        {"wokwi-gate-and-2", Kind::And},   {"wokwi-gate-or-2", Kind::Or},     {"wokwi-gate-xor-2", Kind::Xor},
        {"wokwi-gate-nand-2", Kind::Nand}, {"wokwi-gate-nor-2", Kind::Nor},   {"wokwi-gate-xnor-2", Kind::Xnor},
        {"wokwi-gate-not", Kind::Not},     {"wokwi-gate-buffer", Kind::Buf},  {"wokwi-mux-2", Kind::Mux},
        {"wokwi-flip-flop-d", Kind::Dff},  {"wokwi-flip-flop-dr", Kind::Dffr}, {"wokwi-flip-flop-dsr", Kind::Dffsr},
        {"wokwi-flip-flop-sr", Kind::Srff}, {"wokwi-vcc", Kind::Const},       {"wokwi-gnd", Kind::Const},
        {"chiply-gate-and-3", Kind::And3},   {"chiply-gate-and-4", Kind::And4},   {"chiply-gate-nand-3", Kind::Nand3},
        {"chiply-gate-nand-4", Kind::Nand4}, {"chiply-gate-or-3", Kind::Or3},     {"chiply-gate-or-4", Kind::Or4},
        {"chiply-gate-nor-3", Kind::Nor3},   {"chiply-gate-nor-4", Kind::Nor4},   {"chiply-gate-xor-3", Kind::Xor3},
        {"chiply-maj-3", Kind::Maj3},        {"chiply-mux-4", Kind::Mux4},        {"chiply-a21oi", Kind::A21oi},
        {"chiply-a21o", Kind::A21o},         {"chiply-o21ai", Kind::O21ai},       {"chiply-o21a", Kind::O21a},
        {"chiply-a22oi", Kind::A22oi},       {"chiply-o22ai", Kind::O22ai},
    };
    static const std::map<Kind, std::vector<const char*>> inputs = {
        {Kind::And, {"A", "B"}},  {Kind::Or, {"A", "B"}},   {Kind::Xor, {"A", "B"}},         {Kind::Nand, {"A", "B"}},
        {Kind::Nor, {"A", "B"}},  {Kind::Xnor, {"A", "B"}}, {Kind::Not, {"IN"}},             {Kind::Buf, {"IN"}},
        {Kind::Mux, {"A", "B", "SEL"}}, {Kind::Dff, {"D", "CLK"}}, {Kind::Dffr, {"D", "CLK", "R"}},
        {Kind::Dffsr, {"D", "CLK", "S", "R"}}, {Kind::Srff, {"S", "CLK", "R"}}, {Kind::Const, {}},
        {Kind::And3, {"A", "B", "C"}},  {Kind::And4, {"A", "B", "C", "D"}},  {Kind::Nand3, {"A", "B", "C"}},
        {Kind::Nand4, {"A", "B", "C", "D"}}, {Kind::Or3, {"A", "B", "C"}},  {Kind::Or4, {"A", "B", "C", "D"}},
        {Kind::Nor3, {"A", "B", "C"}},  {Kind::Nor4, {"A", "B", "C", "D"}},  {Kind::Xor3, {"A", "B", "C"}},
        {Kind::Maj3, {"A", "B", "C"}},  {Kind::Mux4, {"A", "B", "C", "D", "S0", "S1"}},
        {Kind::A21oi, {"A1", "A2", "B1"}}, {Kind::A21o, {"A1", "A2", "B1"}}, {Kind::O21ai, {"A1", "A2", "B1"}},
        {Kind::O21a, {"A1", "A2", "B1"}}, {Kind::A22oi, {"A1", "A2", "B1", "B2"}}, {Kind::O22ai, {"A1", "A2", "B1", "B2"}},
    };

    std::vector<int> padPulls; // nets with a pad pull-down
    for (const Device& d : nl.devices) {
        auto pinNet = [&](const std::string& name) {
            for (std::size_t i = 0; i < d.pinNames.size(); ++i)
                if (d.pinNames[i] == name)
                    return d.pinNets[i];
            return -1;
        };
        // A primitive reading `ins` and driving `outs` (strong unless weak).
        auto addPrim = [&](Kind k, std::vector<int> ins, std::vector<int> outs, Strength st = Strength::Strong) {
            Prim p;
            p.kind = k;
            const int idx = int(m_prims.size());
            for (std::size_t i = 0; i < ins.size() && i < 6; ++i) {
                p.in[i] = ins[i];
                if (ins[i] >= 0)
                    m_fanout[size_t(ins[i])].push_back(idx);
            }
            for (std::size_t i = 0; i < outs.size() && i < 2; ++i)
                p.out[i] = addSlot(outs[i], st);
            m_prims.push_back(p);
            return idx;
        };
        const std::string attr = [&](const char* key) {
            return (d.attrs && d.attrs->contains(key) && (*d.attrs)[key].is_string()) ? (*d.attrs)[key].get<std::string>()
                                                                                      : std::string();
        }("value");

        auto k = kinds.find(d.type);
        if (m_opt.chip && k != kinds.end() && k->second != Kind::Const && d.def && d.def->verilog.is_object()
            && d.def->verilog.contains("cell")) {
            // Simulated by the chip backend.
        } else if (k != kinds.end()) {
            std::vector<int> ins;
            for (const char* n : inputs.at(k->second))
                ins.push_back(pinNet(n));
            if (k->second == Kind::Const) {
                const bool vcc = d.type == "wokwi-vcc";
                const int idx = addPrim(Kind::Const, {}, {pinNet(vcc ? "VCC" : "GND")});
                m_prims[size_t(idx)].q = vcc ? V::H : V::L;
            } else if (isFlop(int(k->second))) {
                addPrim(k->second, ins, {pinNet("Q"), pinNet("NOTQ")});
            } else {
                const int idx = addPrim(k->second, ins, {pinNet("OUT")});
                m_prims[size_t(idx)].delay = m_opt.gateDelay;
            }
        } else if (!m_opt.board && d.type.rfind("board-tt-block", 0) == 0) {
            // chip-only mode: the testbench drives the design-side pins
        } else if (d.type == "board-tt-block-input" || d.type == "board-tt-block-input-8") {
            // Each input pad: pass EXT* in, with a very weak pull-down on the
            // pad so an open switch reads (and shows) 0, as on Wokwi.
            auto pad = [&](const std::string& ext, const std::string& inside) {
                addPrim(Kind::TtIn, {pinNet(ext)}, {pinNet(inside)});
                padPulls.push_back(pinNet(ext));
            };
            for (int b = 0; b < 8; ++b)
                pad("EXTIN" + std::to_string(b), "IN" + std::to_string(b));
            if (d.type == "board-tt-block-input") {
                pad("EXTCLK", "CLK");
                pad("EXTRST_N", "RST_N");
            }
        } else if (d.type == "board-tt-block-output") {
            for (int b = 0; b < 8; ++b)
                addPrim(Kind::TtOut, {pinNet("OUT" + std::to_string(b))}, {pinNet("EXTOUT" + std::to_string(b))});
        } else if (d.type == "board-tt-block-bidirectional-io") {
            addPrim(Kind::TtTri, {pinNet("OUT"), pinNet("OE")}, {pinNet("UIO")});
            addPrim(Kind::TtIn, {pinNet("UIO")}, {pinNet("IN")});
            padPulls.push_back(pinNet("UIO"));
        } else if (d.type == "wokwi-resistor") {
            addPrim(Kind::Res, {pinNet("1"), pinNet("2")}, {pinNet("1"), pinNet("2")}, Strength::Weak);
        } else if (d.type == "wokwi-pushbutton") {
            m_switches.push_back({d.partId, d.type, {{pinNet("1.l"), pinNet("2.l")}}, {}, {false}});
        } else if (d.type == "wokwi-slide-switch") {
            // Wokwi: value "1" connects the middle pin to pin 3, else to pin 1.
            m_switches.push_back({d.partId, d.type, {{pinNet("2"), pinNet("3")}}, {{pinNet("2"), pinNet("1")}}, {attr == "1"}});
        } else if (d.type == "wokwi-dip-switch-8") {
            Switch s{d.partId, d.type, {}, {}, std::vector<bool>(8, false)};
            for (int b = 1; b <= 8; ++b)
                s.closedWhenOn.push_back({pinNet(std::to_string(b) + "a"), pinNet(std::to_string(b) + "b")});
            m_switches.push_back(std::move(s));
        } else if (d.type == "wokwi-clock-generator") {
            ClockGen c;
            c.partId = d.partId;
            std::string f = (d.attrs && d.attrs->contains("frequency") && (*d.attrs)["frequency"].is_string())
                ? (*d.attrs)["frequency"].get<std::string>()
                : std::string("10k");
            c.hz = parseFrequency(f).value_or(10000.0);
            c.half = std::max<Time>(1, Time(std::llround(1e12 / (2 * c.hz))));
            c.slot = addSlot(pinNet("CLK"));
            m_clocks.push_back(c);
        } else if (!kInert.count(d.type)) {
            m_warnings.push_back("not simulated: " + d.partId + " (" + d.type + ")");
        }
    }
    if (m_opt.chip) {
        // Chip inputs, as the Verilog export names them: the first input
        // block's CLK, RST_N, IN0..7; uio_in[n] from the bidirectional block
        // with verilogBit n (a later block wins, as in the export).
        m_chipIn.assign(18, -1);
        bool haveIn = false;
        for (const Device& d : nl.devices) {
            auto pinNet = [&](const std::string& name) {
                for (std::size_t i = 0; i < d.pinNames.size(); ++i)
                    if (d.pinNames[i] == name)
                        return d.pinNets[i];
                return -1;
            };
            if (!haveIn && (d.type == "board-tt-block-input" || d.type == "board-tt-block-input-8")) {
                haveIn = true;
                m_chipIn[0] = pinNet("CLK");
                m_chipIn[1] = pinNet("RST_N");
                for (int b = 0; b < 8; ++b)
                    m_chipIn[size_t(2 + b)] = pinNet("IN" + std::to_string(b));
            } else if (d.type == "board-tt-block-bidirectional-io" && d.attrs && d.attrs->contains("verilogBit")) {
                const Json& bit = (*d.attrs)["verilogBit"];
                const std::string s = bit.is_string() ? bit.get<std::string>() : bit.dump();
                if (s.size() == 1 && s[0] >= '0' && s[0] <= '7')
                    m_chipIn[size_t(10 + (s[0] - '0'))] = pinNet("IN");
            }
        }
        Prim p;
        p.kind = Kind::Chip;
        m_chipPrim = int(m_prims.size());
        m_prims.push_back(p);
        for (int net : m_chipIn)
            if (net >= 0)
                m_fanout[size_t(net)].push_back(m_chipPrim);
        for (int net : m_opt.chip->nets())
            m_chipSlots.push_back(addSlot(net, Strength::Strong));
    }
    for (int net : padPulls) {
        const int slot = addSlot(net, Strength::Pad);
        if (slot >= 0)
            m_slots[size_t(slot)].v = V::L;
    }
    m_queued.assign(m_prims.size(), 0);
    m_rng.seed(m_opt.seed ? m_opt.seed : std::random_device{}());
    initialise();
}

int Simulator::addSlot(int net, Strength st)
{
    if (net < 0)
        return -1;
    m_slots.push_back({net, V::Z, st});
    m_netSlots[size_t(net)].push_back(int(m_slots.size()) - 1);
    return int(m_slots.size()) - 1;
}

void Simulator::setSlot(int slot, V v)
{
    if (slot < 0 || m_slots[size_t(slot)].v == v)
        return;
    m_slots[size_t(slot)].v = v;
    const int g = m_groupOf[size_t(m_slots[size_t(slot)].net)];
    if (!m_groupDirty[size_t(g)]) {
        m_groupDirty[size_t(g)] = 1;
        m_dirtyGroups.push_back(g);
    }
}

void Simulator::enqueue(int prim)
{
    if (!m_queued[size_t(prim)]) {
        m_queued[size_t(prim)] = 1;
        m_active.push_back(prim);
    }
}

void Simulator::resolveGroup(int g, bool notify)
{
    // The strongest drive level present wins; disagreement at that level
    // gives X; no drive at all gives Z.
    Strength best = Strength::None;
    V v = V::Z;
    bool conflict = false;
    for (int net : m_groupNets[size_t(g)]) {
        for (int s : m_netSlots[size_t(net)]) {
            const Slot& slot = m_slots[size_t(s)];
            if (slot.v == V::Z || slot.strength < best)
                continue;
            if (slot.strength > best) {
                best = slot.strength;
                v = slot.v;
                conflict = false;
            } else if (slot.v != v) {
                conflict = true;
            }
        }
    }
    if (conflict)
        v = V::X;
    const Strength st = best;
    if (v == m_groupValue[size_t(g)] && st == m_groupStrength[size_t(g)])
        return;
    m_groupValue[size_t(g)] = v;
    m_groupStrength[size_t(g)] = st;
    if (m_anyWatched)
        noteWatched(g);
    if (notify)
        for (int net : m_groupNets[size_t(g)])
            for (int p : m_fanout[size_t(net)])
                enqueue(p);
}

void Simulator::noteWatched(int g)
{
    for (int net : m_groupNets[size_t(g)]) {
        if (!m_watched[size_t(net)])
            continue;
        const V v = m_groupValue[size_t(g)];
        if (v != m_watchLast[size_t(net)]) {
            m_watchLast[size_t(net)] = v;
            m_changes.push_back({m_now, net, v});
        }
    }
}

void Simulator::noteAllWatched()
{
    if (!m_anyWatched)
        return;
    for (std::size_t net = 0; net < m_watched.size(); ++net) {
        if (!m_watched[net])
            continue;
        const V v = value(int(net));
        if (v != m_watchLast[net]) {
            m_watchLast[net] = v;
            m_changes.push_back({m_now, int(net), v});
        }
    }
}

void Simulator::watch(int net)
{
    if (net < 0 || size_t(net) >= m_groupOf.size())
        return;
    if (m_watched.size() != m_groupOf.size()) {
        m_watched.assign(m_groupOf.size(), 0);
        m_watchLast.assign(m_groupOf.size(), V::Z);
    }
    if (m_watched[size_t(net)])
        return;
    m_watched[size_t(net)] = 1;
    m_anyWatched = true;
    m_watchLast[size_t(net)] = value(net);
    m_changes.push_back({m_now, net, m_watchLast[size_t(net)]});
}

void Simulator::unwatchAll()
{
    m_watched.clear();
    m_watchLast.clear();
    m_changes.clear();
    m_anyWatched = false;
}

void Simulator::flush()
{
    for (std::size_t i = 0; i < m_dirtyGroups.size(); ++i) {
        const int g = m_dirtyGroups[i];
        m_groupDirty[size_t(g)] = 0;
        resolveGroup(g);
    }
    m_dirtyGroups.clear();
}

void Simulator::regroup()
{
    // Nets joined by closed switches form one node.
    const std::size_t nets = m_groupOf.size();
    std::vector<V> oldV(nets);
    std::vector<Strength> oldS(nets);
    for (std::size_t n = 0; n < nets; ++n) {
        oldV[n] = m_groupValue[size_t(m_groupOf[n])];
        oldS[n] = m_groupStrength[size_t(m_groupOf[n])];
    }
    std::vector<int> parent(nets);
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int x) {
        while (parent[size_t(x)] != x)
            x = parent[size_t(x)] = parent[size_t(parent[size_t(x)])];
        return x;
    };
    auto join = [&](const std::pair<int, int>& e) {
        if (e.first >= 0 && e.second >= 0)
            parent[size_t(find(e.first))] = find(e.second);
    };
    for (const Switch& s : m_switches) {
        for (std::size_t i = 0; i < s.closedWhenOn.size(); ++i)
            if (s.on[std::min(i, s.on.size() - 1)])
                join(s.closedWhenOn[i]);
        for (std::size_t i = 0; i < s.closedWhenOff.size(); ++i)
            if (!s.on[std::min(i, s.on.size() - 1)])
                join(s.closedWhenOff[i]);
    }
    for (auto& g : m_groupNets)
        g.clear();
    for (std::size_t n = 0; n < nets; ++n) {
        m_groupOf[n] = find(int(n));
        m_groupNets[size_t(m_groupOf[n])].push_back(int(n));
    }
    for (std::size_t g = 0; g < nets; ++g) {
        m_groupDirty[g] = 0;
        m_groupValue[g] = V::Z;
        m_groupStrength[g] = Strength::None;
        if (!m_groupNets[g].empty())
            resolveGroup(int(g), false);
    }
    m_dirtyGroups.clear();
    noteAllWatched(); // a node that went back to Z resolves without a change
    for (std::size_t n = 0; n < nets; ++n) {
        const int g = m_groupOf[n];
        if (m_groupValue[size_t(g)] != oldV[n] || m_groupStrength[size_t(g)] != oldS[n])
            for (int p : m_fanout[n])
                enqueue(p);
    }
}

V Simulator::strongValue(int net) const
{
    if (net < 0)
        return V::Z;
    const int g = m_groupOf[size_t(net)];
    return m_groupStrength[size_t(g)] == Strength::Strong ? m_groupValue[size_t(g)] : V::Z;
}

void Simulator::evaluate(int idx)
{
    Prim& p = m_prims[size_t(idx)];
    ++m_evals;
    auto val = [&](int k) {
        const V v = p.in[k] >= 0 ? m_groupValue[size_t(m_groupOf[size_t(p.in[k])])] : V::Z;
        // Wokwi logic: an input is 1 only if driven to 1; floating or
        // unknown reads 0. Pads and resistors see the raw value.
        if (m_opt.wokwiLogic && p.kind != Kind::TtIn && p.kind != Kind::TtOut && p.kind != Kind::Res)
            return v == V::H ? V::H : V::L;
        return v;
    };
    auto out = [&](V v) {
        if (p.delay > 0 && !m_initialising)
            m_timed.push({m_now + p.delay, m_seq++, p.out[0], v});
        else
            setSlot(p.out[0], v);
    };
    switch (p.kind) {
    case Kind::And: out(vand(val(0), val(1))); return;
    case Kind::Or: out(vor(val(0), val(1))); return;
    case Kind::Xor: out(vxor(val(0), val(1))); return;
    case Kind::Nand: out(vnot(vand(val(0), val(1)))); return;
    case Kind::Nor: out(vnot(vor(val(0), val(1)))); return;
    case Kind::Xnor: out(vnot(vxor(val(0), val(1)))); return;
    case Kind::Not: out(vnot(val(0))); return;
    case Kind::Buf: out(in(val(0))); return;
    case Kind::Mux: out(vmux(val(0), val(1), val(2))); return;
    case Kind::And3: out(vand(vand(val(0), val(1)), val(2))); return;
    case Kind::And4: out(vand(vand(val(0), val(1)), vand(val(2), val(3)))); return;
    case Kind::Nand3: out(vnot(vand(vand(val(0), val(1)), val(2)))); return;
    case Kind::Nand4: out(vnot(vand(vand(val(0), val(1)), vand(val(2), val(3))))); return;
    case Kind::Or3: out(vor(vor(val(0), val(1)), val(2))); return;
    case Kind::Or4: out(vor(vor(val(0), val(1)), vor(val(2), val(3)))); return;
    case Kind::Nor3: out(vnot(vor(vor(val(0), val(1)), val(2)))); return;
    case Kind::Nor4: out(vnot(vor(vor(val(0), val(1)), vor(val(2), val(3))))); return;
    case Kind::Xor3: out(vxor(vxor(val(0), val(1)), val(2))); return;
    case Kind::Maj3: out(vor(vor(vand(val(0), val(1)), vand(val(0), val(2))), vand(val(1), val(2)))); return;
    case Kind::Mux4: out(vmux(vmux(val(0), val(1), val(4)), vmux(val(2), val(3), val(4)), val(5))); return;
    case Kind::A21oi: out(vnot(vor(vand(val(0), val(1)), val(2)))); return;
    case Kind::A21o: out(vor(vand(val(0), val(1)), val(2))); return;
    case Kind::O21ai: out(vnot(vand(vor(val(0), val(1)), val(2)))); return;
    case Kind::O21a: out(vand(vor(val(0), val(1)), val(2))); return;
    case Kind::A22oi: out(vnot(vor(vand(val(0), val(1)), vand(val(2), val(3))))); return;
    case Kind::O22ai: out(vnot(vand(vor(val(0), val(1)), vor(val(2), val(3))))); return;
    case Kind::Const: setSlot(p.out[0], p.q); return;
    case Kind::TtIn: { // a floating pad reads 0, as on Wokwi
        const V v = val(0);
        setSlot(p.out[0], v == V::Z ? V::L : v);
        return;
    }
    case Kind::TtOut: setSlot(p.out[0], val(0)); return;
    case Kind::TtTri: {
        const V oe = in(val(1));
        setSlot(p.out[0], oe == V::H ? in(val(0)) : oe == V::L ? V::Z : V::X);
        return;
    }
    case Kind::Res:
        setSlot(p.out[0], strongValue(p.in[1]));
        setSlot(p.out[1], strongValue(p.in[0]));
        return;
    case Kind::Chip: evaluateChip(); return;
    default: break;
    }

    // Flip-flops: compute the next state now, apply it in the update phase.
    const V clk = val(1);
    const bool edge = !m_initialising && rising(p.prevClk, clk);
    p.prevClk = clk;
    V next = p.pending ? p.nq : p.q;
    switch (p.kind) {
    case Kind::Dff:
        if (edge)
            next = in(val(0));
        break;
    case Kind::Dffr:
        if (in(val(2)) == V::H)
            next = V::L;
        else if (edge)
            next = in(val(0));
        break;
    case Kind::Dffsr:
        if (in(val(3)) == V::H)
            next = V::L;
        else if (in(val(2)) == V::H)
            next = V::H;
        else if (edge)
            next = in(val(0));
        break;
    case Kind::Srff:
        if (edge) {
            const V s = in(val(0)), r = in(val(2));
            if (s == V::H && r == V::L)
                next = V::H;
            else if (r == V::H && s == V::L)
                next = V::L;
            else if (s == V::H && r == V::H)
                next = vnot(p.q); // Wokwi: both set toggles
            else if (s == V::X || r == V::X)
                next = V::X;
        }
        break;
    default: break;
    }
    if (m_initialising) {
        setSlot(p.out[0], p.q);
        setSlot(p.out[1], vnot(p.q));
        return;
    }
    if (next != p.q || p.pending) {
        p.nq = next;
        if (!p.pending) {
            p.pending = true;
            m_nba.push_back(idx);
        }
    }
}

bool Simulator::settle()
{
    flush();
    if (m_opt.wokwiLogic) {
        // Wokwi-style: evaluate one gate at a time, each seeing the latest
        // outputs (so symmetric loops such as an SR latch settle instead of
        // flipping in lock-step). Flip-flops still update in their own
        // phase, after the logic has settled.
        const std::size_t limit = std::size_t(m_opt.maxDeltas) * (m_prims.size() + 1);
        std::size_t evals = 0, head = 0;
        while (head < m_active.size() || !m_nba.empty()) {
            if (head == m_active.size()) {
                m_active.clear();
                head = 0;
                std::vector<int> nba;
                nba.swap(m_nba);
                for (int idx : nba) {
                    Prim& p = m_prims[size_t(idx)];
                    p.pending = false;
                    p.q = p.nq;
                    setSlot(p.out[0], p.q);
                    setSlot(p.out[1], vnot(p.q));
                }
                flush();
                continue;
            }
            if (++evals > limit) {
                // Keep the pending work so the next step continues from a
                // consistent state.
                m_active.erase(m_active.begin(), m_active.begin() + long(head));
                m_error = "combinational loop did not settle at t=" + std::to_string(m_now) + " ps";
                return false;
            }
            const int idx = m_active[head++];
            m_queued[size_t(idx)] = 0;
            evaluate(idx);
            flush();
        }
        m_active.clear();
        return true;
    }
    int deltas = 0;
    while (!m_active.empty() || !m_nba.empty()) {
        if (++deltas > m_opt.maxDeltas) {
            // Keep the pending work so the next step continues from a
            // consistent state.
            m_error = "combinational loop did not settle at t=" + std::to_string(m_now) + " ps";
            return false;
        }
        if (m_active.empty()) {
            std::vector<int> nba;
            nba.swap(m_nba);
            for (int idx : nba) {
                Prim& p = m_prims[size_t(idx)];
                p.pending = false;
                p.q = p.nq;
                setSlot(p.out[0], p.q);
                setSlot(p.out[1], vnot(p.q));
            }
            flush();
            continue;
        }
        m_cur.swap(m_active);
        for (int idx : m_cur) {
            m_queued[size_t(idx)] = 0;
            evaluate(idx);
        }
        m_cur.clear();
        flush();
    }
    return true;
}

bool Simulator::advance(Time dt)
{
    bool ok = settle();
    const Time target = m_now + dt;
    for (;;) {
        Time next = target + 1;
        if (!m_timed.empty())
            next = std::min(next, m_timed.top().t);
        for (const ClockGen& c : m_clocks)
            next = std::min(next, c.next);
        if (next > target)
            break;
        m_now = next;
        while (!m_timed.empty() && m_timed.top().t == m_now) {
            setSlot(m_timed.top().slot, m_timed.top().v);
            m_timed.pop();
        }
        for (ClockGen& c : m_clocks) {
            if (c.next == m_now) {
                c.v = c.v == V::H ? V::L : V::H;
                setSlot(c.slot, c.v);
                c.next += c.half;
            }
        }
        ok = settle() && ok;
    }
    m_now = target;
    return ok;
}

void Simulator::drive(int net, V v)
{
    if (net < 0)
        return;
    if (m_extSlot[size_t(net)] < 0)
        m_extSlot[size_t(net)] = addSlot(net);
    setSlot(m_extSlot[size_t(net)], v);
}

V Simulator::value(int net) const
{
    return net < 0 ? V::Z : m_groupValue[size_t(m_groupOf[size_t(net)])];
}

void Simulator::evaluateChip()
{
    // A two-state engine: an input is 1 only if it is driven to 1.
    auto bit = [&](int i) {
        const int net = m_chipIn[size_t(i)];
        return net >= 0 && m_groupValue[size_t(m_groupOf[size_t(net)])] == V::H;
    };
    std::uint8_t ui = 0, uio = 0;
    for (int b = 0; b < 8; ++b) {
        ui |= std::uint8_t(bit(2 + b) << b);
        uio |= std::uint8_t(bit(10 + b) << b);
    }
    m_opt.chip->eval(ui, uio, bit(0), bit(1));
    m_opt.chip->read(m_chipValues);
    for (std::size_t i = 0; i < m_chipSlots.size() && i < m_chipValues.size(); ++i)
        setSlot(m_chipSlots[i], m_chipValues[i] ? V::H : V::L);
}

void Simulator::initialise()
{
    if (m_opt.chip)
        m_opt.chip->reset(m_opt.flopStart, m_rng());
    for (Prim& p : m_prims) {
        if (isFlop(int(p.kind))) {
            switch (m_opt.flopStart) {
            case FlopStart::Zero: p.q = V::L; break;
            case FlopStart::Unknown: p.q = V::X; break;
            case FlopStart::Random: p.q = (m_rng() & 1) ? V::H : V::L; break;
            }
            p.pending = false;
        }
    }
    for (ClockGen& c : m_clocks) {
        c.v = V::L;
        c.next = m_now + c.half;
        setSlot(c.slot, V::L);
    }
    m_nba.clear();
    regroup();
    m_initialising = true;
    for (std::size_t i = 0; i < m_prims.size(); ++i)
        enqueue(int(i));
    settle();
    m_initialising = false;
    for (std::size_t i = 0; i < m_prims.size(); ++i)
        if (isFlop(int(m_prims[i].kind)))
            enqueue(int(i));
    settle();
}

void Simulator::reset()
{
    initialise();
}

bool Simulator::setPressed(const std::string& partId, bool pressed)
{
    for (Switch& s : m_switches)
        if (s.partId == partId && s.type == "wokwi-pushbutton") {
            if (s.on[0] != pressed) {
                s.on[0] = pressed;
                regroup();
                settle();
            }
            return true;
        }
    return false;
}

bool Simulator::setSwitch(const std::string& partId, int index, bool on)
{
    for (Switch& s : m_switches) {
        if (s.partId != partId)
            continue;
        const std::size_t i = s.type == "wokwi-dip-switch-8" ? std::size_t(index) : 0;
        if (i >= s.on.size())
            return false;
        if (s.on[i] != on) {
            s.on[i] = on;
            regroup();
            settle();
        }
        return true;
    }
    return false;
}

std::optional<bool> Simulator::switchState(const std::string& partId, int index) const
{
    for (const Switch& s : m_switches)
        if (s.partId == partId) {
            const std::size_t i = s.type == "wokwi-dip-switch-8" ? std::size_t(index) : 0;
            if (i < s.on.size())
                return s.on[i];
        }
    return std::nullopt;
}

std::optional<bool> Simulator::ledLit(const std::string& partId) const
{
    const int dev = m_nl.deviceOf(partId);
    if (dev < 0 || m_nl.devices[size_t(dev)].type != "wokwi-led")
        return std::nullopt;
    return value(netOf({partId, "A"})) == V::H && value(netOf({partId, "C"})) == V::L;
}

std::optional<unsigned> Simulator::segments(const std::string& partId) const
{
    const int dev = m_nl.deviceOf(partId);
    if (dev < 0 || m_nl.devices[size_t(dev)].type != "wokwi-7segment")
        return std::nullopt;
    const Device& d = m_nl.devices[size_t(dev)];
    std::string common = "anode"; // wokwi-elements default
    if (d.attrs && d.attrs->contains("common") && (*d.attrs)["common"].is_string())
        common = (*d.attrs)["common"].get<std::string>();
    const V com = value(netOf({partId, "COM.1"}));
    unsigned bits = 0;
    static const char* names[] = {"A", "B", "C", "D", "E", "F", "G", "DP"};
    for (int i = 0; i < 8; ++i) {
        const V seg = value(netOf({partId, names[i]}));
        const bool lit = common == "cathode" ? (seg == V::H && com == V::L) : (seg == V::L && com == V::H);
        if (lit)
            bits |= 1u << i;
    }
    return bits;
}

std::vector<Simulator::ClockInfo> Simulator::clocks() const
{
    std::vector<ClockInfo> out;
    for (const ClockGen& c : m_clocks)
        out.push_back({c.partId, c.hz});
    return out;
}

} // namespace chiply::sim
