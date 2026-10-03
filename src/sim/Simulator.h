#pragma once
// Built-in event-driven logic simulator (PLAN.md 6). Plain C++, no Qt.
//
// Compiled from a Netlist into flat arrays. Nets resolve the values of
// their driver slots (gate outputs, constants, external drives); several
// strong drivers that disagree give X, none give Z. Zero-delay evaluation
// uses delta cycles; primitives may instead have a unit delay (timed
// events). Nets can be grouped into electrical nodes (closed switches,
// M7c): a group resolves all slots of all its nets together.
#include "core/Netlist.h"
#include "sim/Value.h"

#include <cstdint>
#include <queue>
#include <string>
#include <vector>

namespace chiply::sim {

using Time = std::int64_t; // picoseconds

struct Options {
    bool flopsStartUnknown = false; // false: flip-flops start at 0, as Wokwi
    Time gateDelay = 0;             // 0: zero-delay (delta cycles); else unit delay per gate
    int maxDeltas = 10000;          // per time step before "did not settle"
};

class Simulator {
public:
    Simulator(const Netlist& nl, Options opt = {});

    int netOf(const PinRef& ref) const { return m_nl.netOf(ref); }
    const Netlist& netlist() const { return m_nl; }

    // Testbench drive on a net (strong). V::Z releases it.
    void drive(int net, V v);
    void drive(const PinRef& ref, V v) { drive(netOf(ref), v); }

    V value(int net) const;
    V value(const PinRef& ref) const { return value(netOf(ref)); }

    // Processes everything due at the current time (delta cycles).
    // Returns false if it did not settle (combinational loop).
    bool settle();
    // Advances simulated time by dt, processing timed events on the way.
    bool advance(Time dt);
    Time now() const { return m_now; }

    // Flip-flops back to their start state; re-evaluates everything.
    void reset();

    // Not supported by the kernel (unknown types, boards): their pins float.
    const std::vector<std::string>& warnings() const { return m_warnings; }
    const std::string& lastError() const { return m_error; }
    std::uint64_t evaluations() const { return m_evals; }

private:
    // Order matters: Dff..Srff are the flip-flops (isFlop in the .cpp).
    enum class Kind : std::uint8_t { And, Or, Xor, Nand, Nor, Xnor, Not, Buf, Mux, Dff, Dffr, Dffsr, Srff, Const };
    struct Prim {
        Kind kind;
        int in[4] = {-1, -1, -1, -1};  // input nets (meaning per kind)
        int out[2] = {-1, -1};         // driver slots (OUT / Q, NOTQ)
        V q = V::L;                    // flip-flop state / constant value
        V nq = V::L;                   // flip-flop next state (applied in the update phase)
        V prevClk = V::X;
        bool pending = false;          // queued for the flip-flop update phase
        Time delay = 0;
    };
    struct Slot {
        int net;
        V v = V::Z;
    };
    struct Timed {
        Time t;
        std::uint64_t seq;
        int slot;
        V v;
        bool operator>(const Timed& o) const { return t != o.t ? t > o.t : seq > o.seq; }
    };

    int addSlot(int net);
    void setSlot(int slot, V v);
    void resolveGroup(int group);
    void evaluate(int prim);
    void initialise();

    const Netlist& m_nl;
    Options m_opt;
    std::vector<Prim> m_prims;
    std::vector<Slot> m_slots;
    std::vector<std::vector<int>> m_netSlots;   // net -> slots
    std::vector<std::vector<int>> m_fanout;     // net -> prims reading it
    std::vector<int> m_groupOf;                 // net -> group
    std::vector<std::vector<int>> m_groupNets;  // group -> nets
    std::vector<V> m_groupValue;
    std::vector<int> m_extSlot;                 // net -> testbench slot (-1)

    std::vector<int> m_active, m_cur;
    std::vector<int> m_nba;                     // flip-flops with a pending update
    bool m_initialising = false;
    void enqueue(int prim);
    void flush();
    std::vector<char> m_queued;
    std::vector<int> m_dirtyGroups;
    std::vector<char> m_groupDirty;
    std::priority_queue<Timed, std::vector<Timed>, std::greater<Timed>> m_timed;
    std::uint64_t m_seq = 0;
    Time m_now = 0;
    std::uint64_t m_evals = 0;
    std::vector<std::string> m_warnings;
    std::string m_error;
};

} // namespace chiply::sim
