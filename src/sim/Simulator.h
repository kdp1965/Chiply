#pragma once
// Built-in event-driven logic simulator (PLAN.md 6). Plain C++, no Qt.
//
// Compiled from a Netlist into flat arrays. Nets are grouped into electrical
// nodes (nets joined by closed switches). A node resolves the values of all
// driver slots on its nets: strong drivers (gates, constants, Tiny Tapeout
// blocks, testbench drives) win over weak ones (resistors); disagreeing
// drivers of the winning strength give X; no driver gives Z.
// Zero-delay evaluation uses delta cycles; gates may have a unit delay.
// Flip-flop updates are applied in a separate phase, like Verilog
// non-blocking assignments.
#include "core/Netlist.h"
#include "sim/Value.h"

#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace chiply::sim {

using Time = std::int64_t; // picoseconds

struct Options {
    bool flopsStartUnknown = false; // false: flip-flops start at 0, as Wokwi
    Time gateDelay = 0;             // 0: zero-delay (delta cycles); else unit delay per gate
    int maxDeltas = 10000;          // per time step before "did not settle"
    // false: simulate the chip only. Tiny Tapeout blocks are passive, so a
    // testbench drives IN*/CLK/RST_N and the uio pins directly, as when
    // verifying against the Verilog export. true: chip plus board, as Wokwi.
    bool board = true;
    // true (default): Wokwi logic. Every input reads 0 or 1: a floating (Z)
    // or unknown (X) input reads 0, so feedback loops start from definite
    // values, as on wokwi.com. Net values still show X/Z for debugging.
    // false: Verilog four-state logic (X propagates), used to verify
    // against the Verilog export.
    bool wokwiLogic = true;
};

// "10000", "10k", "2.5kHz", "1M", "1MHz" -> Hz; nullopt if unparsable.
std::optional<double> parseFrequency(const std::string& s);

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
    // Advances simulated time by dt, processing timed events (gate delays,
    // clock generators) on the way.
    bool advance(Time dt);
    Time now() const { return m_now; }

    // Flip-flops back to their start state; re-evaluates everything.
    void reset();

    // ---- board (PLAN.md 6.2) ----
    // Pushbuttons: pressed connects the 1.x and 2.x contacts.
    bool setPressed(const std::string& partId, bool pressed);
    // Slide switch (index ignored: on = middle pin to pin 3, off = to pin 1)
    // and DIP switch (index 0..7). Returns false if there is no such switch.
    bool setSwitch(const std::string& partId, int index, bool on);
    std::optional<bool> switchState(const std::string& partId, int index = 0) const;
    // LED lit: anode high and cathode not high.
    std::optional<bool> ledLit(const std::string& partId) const;
    // 7-segment: bit 0..6 = segments A..G, bit 7 = DP.
    std::optional<unsigned> segments(const std::string& partId) const;
    // Clock generators found in the design (for pacing in the UI).
    struct ClockInfo {
        std::string partId;
        double hz;
    };
    std::vector<ClockInfo> clocks() const;

    // Not simulated (unknown types): their pins float.
    const std::vector<std::string>& warnings() const { return m_warnings; }
    const std::string& lastError() const { return m_error; }
    std::uint64_t evaluations() const { return m_evals; }

private:
    // Order matters: Dff..Srff are the flip-flops (isFlop in the .cpp).
    enum class Kind : std::uint8_t {
        And, Or, Xor, Nand, Nor, Xnor, Not, Buf, Mux, Dff, Dffr, Dffsr, Srff, Const,
        TtIn,     // Tiny Tapeout input pad: Z reads as 0
        TtOut,    // output pad: pass through
        TtTri,    // bidirectional pad driver: OUT when OE is 1, else Z
        Res       // resistor: weak copy of each side's strong value onto the other
    };
    // Pad < Weak < Strong: a Tiny Tapeout input pad's built-in pull-down
    // loses to a resistor, which loses to any driver.
    enum class Strength : std::uint8_t { None, Pad, Weak, Strong };
    struct Prim {
        Kind kind;
        int in[4] = {-1, -1, -1, -1};  // input nets (meaning per kind)
        int out[2] = {-1, -1};         // driver slots
        V q = V::L;                    // flip-flop state / constant value
        V nq = V::L;                   // flip-flop next state (applied in the update phase)
        V prevClk = V::X;
        bool pending = false;
        Time delay = 0;
    };
    struct Slot {
        int net;
        V v = V::Z;
        Strength strength = Strength::Strong;
    };
    struct Timed {
        Time t;
        std::uint64_t seq;
        int slot;
        V v;
        bool operator>(const Timed& o) const { return t != o.t ? t > o.t : seq > o.seq; }
    };
    struct Switch {
        std::string partId;
        std::string type;
        std::vector<std::pair<int, int>> closedWhenOn;  // net pairs joined when on (per index)
        std::vector<std::pair<int, int>> closedWhenOff; // slide switch: middle-to-pin-1
        std::vector<bool> on;
    };
    struct ClockGen {
        std::string partId;
        int slot = -1;
        double hz = 0;
        Time half = 0; // half period, ps
        Time next = 0;
        V v = V::L;
    };

    int addSlot(int net, Strength s = Strength::Strong);
    void setSlot(int slot, V v);
    void resolveGroup(int group, bool notify = true);
    void regroup();
    void evaluate(int prim);
    void initialise();
    void enqueue(int prim);
    void flush();
    V strongValue(int net) const;

    const Netlist& m_nl;
    Options m_opt;
    std::vector<Prim> m_prims;
    std::vector<Slot> m_slots;
    std::vector<std::vector<int>> m_netSlots;   // net -> slots
    std::vector<std::vector<int>> m_fanout;     // net -> prims reading it
    std::vector<int> m_groupOf;                 // net -> group
    std::vector<std::vector<int>> m_groupNets;  // group -> nets
    std::vector<V> m_groupValue;
    std::vector<Strength> m_groupStrength;
    std::vector<int> m_extSlot;                 // net -> testbench slot (-1)
    std::vector<Switch> m_switches;
    std::vector<ClockGen> m_clocks;

    std::vector<int> m_active, m_cur;
    std::vector<int> m_nba;
    std::vector<char> m_queued;
    std::vector<int> m_dirtyGroups;
    std::vector<char> m_groupDirty;
    std::priority_queue<Timed, std::vector<Timed>, std::greater<Timed>> m_timed;
    std::uint64_t m_seq = 0;
    Time m_now = 0;
    std::uint64_t m_evals = 0;
    bool m_initialising = false;
    std::vector<std::string> m_warnings;
    std::string m_error;
};

} // namespace chiply::sim
