#include "sim/Simulator.h"

#include <map>
#include <set>

namespace chiply::sim {

namespace {

// Parts the kernel does not simulate itself (board and display parts are
// handled by the board layer, M7c); no warning for these.
const std::set<std::string> kPassiveTypes = {
    "wokwi-text", "wokwi-junction", "board-tt-block-input", "board-tt-block-input-8", "board-tt-block-output",
    "board-tt-block-bidirectional-io", "wokwi-clock-generator", "wokwi-pushbutton", "wokwi-slide-switch",
    "wokwi-dip-switch-8", "wokwi-resistor", "wokwi-led", "wokwi-7segment", "wokwi-logic-analyzer", "wokwi-pi-pico"};

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

} // namespace

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
    m_groupDirty.assign(nets, 0);
    m_extSlot.assign(nets, -1);
    for (std::size_t n = 0; n < nets; ++n) {
        m_groupOf[n] = int(n);
        m_groupNets[n] = {int(n)};
    }

    static const std::map<std::string, Kind> kinds = {
        {"wokwi-gate-and-2", Kind::And},   {"wokwi-gate-or-2", Kind::Or},     {"wokwi-gate-xor-2", Kind::Xor},
        {"wokwi-gate-nand-2", Kind::Nand}, {"wokwi-gate-nor-2", Kind::Nor},   {"wokwi-gate-xnor-2", Kind::Xnor},
        {"wokwi-gate-not", Kind::Not},     {"wokwi-gate-buffer", Kind::Buf},  {"wokwi-mux-2", Kind::Mux},
        {"wokwi-flip-flop-d", Kind::Dff},  {"wokwi-flip-flop-dr", Kind::Dffr}, {"wokwi-flip-flop-dsr", Kind::Dffsr},
        {"wokwi-flip-flop-sr", Kind::Srff}, {"wokwi-vcc", Kind::Const},       {"wokwi-gnd", Kind::Const},
    };
    // Input pin order per kind (see evaluate()).
    static const std::map<Kind, std::vector<const char*>> inputs = {
        {Kind::And, {"A", "B"}},  {Kind::Or, {"A", "B"}},   {Kind::Xor, {"A", "B"}},         {Kind::Nand, {"A", "B"}},
        {Kind::Nor, {"A", "B"}},  {Kind::Xnor, {"A", "B"}}, {Kind::Not, {"IN"}},             {Kind::Buf, {"IN"}},
        {Kind::Mux, {"A", "B", "SEL"}}, {Kind::Dff, {"D", "CLK"}}, {Kind::Dffr, {"D", "CLK", "R"}},
        {Kind::Dffsr, {"D", "CLK", "S", "R"}}, {Kind::Srff, {"S", "CLK", "R"}}, {Kind::Const, {}},
    };

    for (const Device& d : nl.devices) {
        auto k = kinds.find(d.type);
        if (k == kinds.end()) {
            if (!kPassiveTypes.count(d.type))
                m_warnings.push_back("not simulated: " + d.partId + " (" + d.type + ")");
            continue;
        }
        auto pinNet = [&](const char* name) {
            for (std::size_t i = 0; i < d.pinNames.size(); ++i)
                if (d.pinNames[i] == name)
                    return d.pinNets[i];
            return -1;
        };
        Prim p;
        p.kind = k->second;
        const int idx = int(m_prims.size());
        const auto& ins = inputs.at(p.kind);
        for (std::size_t i = 0; i < ins.size(); ++i) {
            p.in[i] = pinNet(ins[i]);
            if (p.in[i] >= 0)
                m_fanout[size_t(p.in[i])].push_back(idx);
        }
        if (p.kind == Kind::Const) {
            p.q = d.type == "wokwi-vcc" ? V::H : V::L;
            p.out[0] = addSlot(pinNet(d.type == "wokwi-vcc" ? "VCC" : "GND"));
        } else if (isFlop(int(p.kind))) {
            p.out[0] = addSlot(pinNet("Q"));
            p.out[1] = addSlot(pinNet("NOTQ"));
        } else {
            p.out[0] = addSlot(pinNet("OUT"));
            p.delay = m_opt.gateDelay;
        }
        m_prims.push_back(p);
    }
    m_queued.assign(m_prims.size(), 0);
    initialise();
}

int Simulator::addSlot(int net)
{
    if (net < 0)
        return -1;
    m_slots.push_back({net, V::Z});
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

void Simulator::resolveGroup(int g)
{
    bool any = false, conflict = false;
    V v = V::Z;
    for (int net : m_groupNets[size_t(g)]) {
        for (int s : m_netSlots[size_t(net)]) {
            const V sv = m_slots[size_t(s)].v;
            if (sv == V::Z)
                continue;
            if (!any) {
                v = sv;
                any = true;
            } else if (sv != v) {
                conflict = true;
            }
        }
    }
    if (conflict)
        v = V::X;
    if (v == m_groupValue[size_t(g)])
        return;
    m_groupValue[size_t(g)] = v;
    for (int net : m_groupNets[size_t(g)])
        for (int p : m_fanout[size_t(net)])
            enqueue(p);
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

void Simulator::evaluate(int idx)
{
    Prim& p = m_prims[size_t(idx)];
    ++m_evals;
    auto val = [&](int k) { return p.in[k] >= 0 ? m_groupValue[size_t(m_groupOf[size_t(p.in[k])])] : V::Z; };
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
    case Kind::Const: setSlot(p.out[0], p.q); return;
    default: break;
    }

    // Flip-flops: compute the next state now, apply it in the update phase
    // (like Verilog non-blocking assignments), so clocks that pass through
    // gates still see the old data.
    const V clk = val(1);
    const bool edge = !m_initialising && rising(p.prevClk, clk);
    p.prevClk = clk;
    V next = p.pending ? p.nq : p.q;
    switch (p.kind) {
    case Kind::Dff:
        if (edge)
            next = in(val(0));
        break;
    case Kind::Dffr: // cells.v dffr_cell: async reset
        if (in(val(2)) == V::H)
            next = V::L;
        else if (edge)
            next = in(val(0));
        break;
    case Kind::Dffsr: // cells.v dffsr_cell: async reset wins over set
        if (in(val(3)) == V::H)
            next = V::L;
        else if (in(val(2)) == V::H)
            next = V::H;
        else if (edge)
            next = in(val(0));
        break;
    case Kind::Srff: { // Wokwi flip-flop-sr: clocked set/reset
        if (edge) {
            const V s = in(val(0)), r = in(val(2));
            if (s == V::H && r == V::L)
                next = V::H;
            else if (r == V::H && s == V::L)
                next = V::L;
            else if (s == V::H && r == V::H)
                next = V::X;
            else if (s == V::X || r == V::X)
                next = V::X;
        }
        break;
    }
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
    int deltas = 0;
    while (!m_active.empty() || !m_nba.empty()) {
        if (++deltas > m_opt.maxDeltas) {
            m_error = "combinational loop did not settle at t=" + std::to_string(m_now) + " ps";
            for (int p : m_active)
                m_queued[size_t(p)] = 0;
            m_active.clear();
            return false;
        }
        if (m_active.empty()) {
            // Update phase: apply flip-flop next states.
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
    while (!m_timed.empty() && m_timed.top().t <= target) {
        m_now = m_timed.top().t;
        while (!m_timed.empty() && m_timed.top().t == m_now) {
            setSlot(m_timed.top().slot, m_timed.top().v);
            m_timed.pop();
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

void Simulator::initialise()
{
    const V start = m_opt.flopsStartUnknown ? V::X : V::L;
    for (Prim& p : m_prims) {
        if (isFlop(int(p.kind))) {
            p.q = start;
            p.pending = false;
        }
    }
    m_nba.clear();
    m_initialising = true;
    for (std::size_t i = 0; i < m_prims.size(); ++i)
        enqueue(int(i));
    // First pass: settle combinational logic with flip-flops held at their
    // start value and clocks captured without edges.
    settle();
    m_initialising = false;
    // Second pass: async set/reset that are already active take effect.
    for (std::size_t i = 0; i < m_prims.size(); ++i)
        if (isFlop(int(m_prims[i].kind)))
            enqueue(int(i));
    settle();
}

void Simulator::reset()
{
    initialise();
}

} // namespace chiply::sim
