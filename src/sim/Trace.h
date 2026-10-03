#pragma once
// Recorded waveforms (PLAN.md 6.4): logic analyzer channels and probed nets,
// written as VCD. Plain C++, no Qt.
#include "sim/Simulator.h"

#include <ostream>
#include <string>
#include <vector>

namespace chiply::sim {

struct TraceSignal {
    std::string scope;  // VCD scope: analyzer part id, or "probes"
    std::string name;   // VCD variable name
    int net = -1;       // -1: an unconnected pin (stays Z)
    // Value changes in time order; samples[0] is the value when recording
    // started (or when older samples were dropped).
    std::vector<std::pair<Time, V>> samples;

    // Value at time t (Z before the first sample).
    V at(Time t) const;
};

class Trace {
public:
    // Keeps at most this many changes per signal; older ones are dropped.
    explicit Trace(std::size_t maxSamplesPerSignal = 1'000'000) : m_max(maxSamplesPerSignal) {}

    // Records a net from now on. Returns the signal index; a signal with the
    // same scope and name is reused.
    int add(Simulator& sim, const std::string& scope, const std::string& name, int net);
    void remove(int index);
    // One signal per channel of every wokwi-logic-analyzer in the design:
    // scope = part id, names D0..D7 (attr "channels", default 8).
    void addLogicAnalyzers(Simulator& sim);
    // Default VCD file name: the first analyzer's "filename" attr (Wokwi's
    // default "wokwi-logic") + ".vcd".
    std::string defaultFileName() const { return m_fileName + ".vcd"; }
    bool hasAnalyzer() const { return m_hasAnalyzer; }

    // Moves the simulator's logged changes into the signals.
    void collect(Simulator& sim);
    // Drops all samples; each signal restarts at its current value.
    void restart(Simulator& sim);

    const std::vector<TraceSignal>& channels() const { return m_signals; }
    // Earliest time still covered by every signal.
    Time start() const;

    // VCD with a 1 ps timescale, ending at `end`.
    void writeVcd(std::ostream& out, Time end) const;

private:
    void append(TraceSignal& s, Time t, V v);

    std::size_t m_max;
    std::vector<TraceSignal> m_signals;
    std::string m_fileName = "wokwi-logic";
    bool m_hasAnalyzer = false;
};

// VCD-safe identifier: letters, digits and '_' ("flop30:Q" -> "flop30_Q").
std::string vcdName(const std::string& s);

} // namespace chiply::sim
