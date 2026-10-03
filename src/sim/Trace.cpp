#include "sim/Trace.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace chiply::sim {

V TraceSignal::at(Time t) const
{
    auto it = std::upper_bound(samples.begin(), samples.end(), t,
                               [](Time x, const std::pair<Time, V>& s) { return x < s.first; });
    return it == samples.begin() ? V::Z : std::prev(it)->second;
}

std::string vcdName(const std::string& s)
{
    std::string r;
    for (char c : s)
        r += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    return r.empty() ? std::string("_") : r;
}

int Trace::add(Simulator& sim, const std::string& scope, const std::string& name, int net)
{
    for (std::size_t i = 0; i < m_signals.size(); ++i)
        if (m_signals[i].scope == scope && m_signals[i].name == name)
            return int(i);
    TraceSignal s{scope, name, net, {}};
    if (net >= 0) {
        sim.watch(net);
        // watch() logs the start value only the first time a net is
        // watched; seed this signal directly so it never starts empty.
        s.samples.push_back({sim.now(), sim.value(net)});
    } else {
        s.samples.push_back({sim.now(), V::Z});
    }
    m_signals.push_back(std::move(s));
    return int(m_signals.size() - 1);
}

void Trace::remove(int index)
{
    if (index >= 0 && size_t(index) < m_signals.size())
        m_signals.erase(m_signals.begin() + index);
}

void Trace::addLogicAnalyzers(Simulator& sim)
{
    for (const Device& d : sim.netlist().devices) {
        if (d.type != "wokwi-logic-analyzer")
            continue;
        int channels = 8;
        if (d.attrs && d.attrs->contains("channels")) {
            const Json& c = (*d.attrs)["channels"];
            try {
                channels = c.is_string() ? std::stoi(c.get<std::string>()) : c.is_number() ? c.get<int>() : 8;
            } catch (...) {
            }
            channels = std::clamp(channels, 1, 8);
        }
        if (!m_hasAnalyzer && d.attrs && d.attrs->contains("filename") && (*d.attrs)["filename"].is_string()
            && !(*d.attrs)["filename"].get<std::string>().empty())
            m_fileName = (*d.attrs)["filename"].get<std::string>();
        m_hasAnalyzer = true;
        for (int i = 0; i < channels; ++i) {
            const std::string pin = "D" + std::to_string(i);
            add(sim, d.partId, pin, sim.netOf(PinRef{d.partId, pin}));
        }
    }
}

void Trace::append(TraceSignal& s, Time t, V v)
{
    if (!s.samples.empty() && s.samples.back().first == t) {
        // Several changes in one time step (delta cycles): keep the last,
        // and drop it if that is no change after all.
        s.samples.back().second = v;
        if (s.samples.size() > 1 && s.samples[s.samples.size() - 2].second == v)
            s.samples.pop_back();
        return;
    }
    if (!s.samples.empty() && s.samples.back().second == v)
        return;
    s.samples.push_back({t, v});
    if (s.samples.size() > m_max)
        s.samples.erase(s.samples.begin(), s.samples.begin() + std::ptrdiff_t(s.samples.size() / 2));
}

void Trace::collect(Simulator& sim)
{
    const auto changes = sim.takeChanges();
    if (changes.empty())
        return;
    std::map<int, std::vector<TraceSignal*>> byNet;
    for (TraceSignal& s : m_signals)
        if (s.net >= 0)
            byNet[s.net].push_back(&s);
    for (const Simulator::Change& c : changes) {
        auto it = byNet.find(c.net);
        if (it == byNet.end())
            continue;
        for (TraceSignal* s : it->second)
            if (s->samples.empty() || c.t >= s->samples.back().first)
                append(*s, c.t, c.v);
    }
}

void Trace::restart(Simulator& sim)
{
    sim.takeChanges();
    for (TraceSignal& s : m_signals)
        s.samples = {{sim.now(), s.net >= 0 ? sim.value(s.net) : V::Z}};
}

Time Trace::start() const
{
    Time t = 0;
    bool any = false;
    for (const TraceSignal& s : m_signals)
        if (!s.samples.empty()) {
            t = any ? std::max(t, s.samples.front().first) : s.samples.front().first;
            any = true;
        }
    return t;
}

void Trace::writeVcd(std::ostream& out, Time end) const
{
    auto id = [](std::size_t i) {
        std::string s;
        do {
            s += char('!' + i % 94);
            i /= 94;
        } while (i);
        return s;
    };
    auto ch = [](V v) {
        switch (v) {
        case V::L: return '0';
        case V::H: return '1';
        case V::X: return 'x';
        case V::Z: return 'z';
        }
        return 'x';
    };
    out << "$version Chiply $end\n$timescale 1ps $end\n";
    std::vector<std::string> scopes;
    for (const TraceSignal& s : m_signals)
        if (std::find(scopes.begin(), scopes.end(), s.scope) == scopes.end())
            scopes.push_back(s.scope);
    for (const std::string& sc : scopes) {
        out << "$scope module " << vcdName(sc) << " $end\n";
        for (std::size_t i = 0; i < m_signals.size(); ++i)
            if (m_signals[i].scope == sc)
                out << "$var wire 1 " << id(i) << " " << vcdName(m_signals[i].name) << " $end\n";
        out << "$upscope $end\n";
    }
    out << "$enddefinitions $end\n";

    // Merge all signals' changes in time order.
    struct Ev {
        Time t;
        std::size_t sig;
        V v;
    };
    std::vector<Ev> evs;
    const Time t0 = start();
    for (std::size_t i = 0; i < m_signals.size(); ++i) {
        const auto& sm = m_signals[i].samples;
        evs.push_back({t0, i, m_signals[i].at(t0)});
        for (const auto& [t, v] : sm)
            if (t > t0 && t <= end)
                evs.push_back({t, i, v});
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    Time cur = -1;
    bool dumpvars = true;
    for (const Ev& e : evs) {
        if (e.t != cur) {
            if (dumpvars && cur >= 0) {
                out << "$end\n";
                dumpvars = false;
            }
            out << "#" << e.t << "\n";
            if (cur < 0)
                out << "$dumpvars\n";
            cur = e.t;
        }
        out << ch(e.v) << id(e.sig) << "\n";
    }
    if (dumpvars && cur >= 0)
        out << "$end\n";
    if (end > cur)
        out << "#" << end << "\n";
}

} // namespace chiply::sim
