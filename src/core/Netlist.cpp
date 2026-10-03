#include "core/Netlist.h"

#include <numeric>

namespace chiply {

namespace {

struct UnionFind {
    std::vector<int> parent;
    int add()
    {
        parent.push_back(int(parent.size()));
        return parent.back();
    }
    int find(int x)
    {
        while (parent[size_t(x)] != x) {
            parent[size_t(x)] = parent[size_t(parent[size_t(x)])];
            x = parent[size_t(x)];
        }
        return x;
    }
    void unite(int a, int b) { parent[size_t(find(a))] = find(b); }
};

bool isDriver(PinDir d)
{
    return d == PinDir::Out || d == PinDir::InOut || d == PinDir::Power;
}

} // namespace

Netlist Netlist::build(const Document& doc, const PartLibrary& lib)
{
    Netlist nl;
    UnionFind uf;
    std::vector<std::pair<int, int>> pinOf; // global pin -> (device, pin)
    std::vector<std::vector<int>> globalPin; // [device][pin] -> global pin

    auto addPin = [&](int dev, const std::string& name, PinDir dir) {
        Device& d = nl.devices[size_t(dev)];
        d.pinNames.push_back(name);
        d.pinDirs.push_back(dir);
        const int g = uf.add();
        pinOf.emplace_back(dev, int(d.pinNames.size()) - 1);
        globalPin[size_t(dev)].push_back(g);
        return g;
    };

    for (const Part& p : doc.parts) {
        if (p.type == "wokwi-text")
            continue; // annotation, no pins
        Device d;
        d.partId = p.id;
        d.type = p.type;
        d.def = lib.find(p.type);
        d.attrs = &p.attrs;
        const int dev = int(nl.devices.size());
        if (nl.m_deviceIndex.count(p.id))
            nl.warnings.push_back("duplicate part id \"" + p.id + "\"");
        nl.m_deviceIndex[p.id] = dev;
        nl.devices.push_back(std::move(d));
        globalPin.emplace_back();
        if (const PartDef* def = nl.devices.back().def)
            for (const PinDef& pin : def->pins)
                addPin(dev, pin.name, pin.dir);
        else
            nl.warnings.push_back("unknown part type \"" + p.type + "\" (" + p.id + ")");
    }

    // Pins a part connects internally: same prefix before '.'.
    for (std::size_t dev = 0; dev < nl.devices.size(); ++dev) {
        const Device& d = nl.devices[dev];
        std::map<std::string, int> first;
        for (std::size_t i = 0; i < d.pinNames.size(); ++i) {
            const std::string& n = d.pinNames[i];
            const auto dot = n.find('.');
            if (dot == std::string::npos || dot == 0)
                continue;
            auto [it, inserted] = first.emplace(n.substr(0, dot), globalPin[dev][i]);
            if (!inserted)
                uf.unite(globalPin[dev][i], it->second);
        }
    }

    auto pinIndex = [&](const PinRef& r) -> int {
        auto it = nl.m_deviceIndex.find(r.part);
        if (it == nl.m_deviceIndex.end()) {
            nl.warnings.push_back("wire to missing part \"" + r.part + "\"");
            return -1;
        }
        Device& d = nl.devices[size_t(it->second)];
        for (std::size_t i = 0; i < d.pinNames.size(); ++i)
            if (d.pinNames[i] == r.pin)
                return globalPin[size_t(it->second)][i];
        // Unknown pin: keep the connection (and report it) so nothing is lost.
        if (d.def)
            nl.warnings.push_back("wire to unknown pin \"" + r.str() + "\"");
        return addPin(it->second, r.pin, PinDir::Passive);
    };
    for (const Wire& w : doc.wires) {
        const int a = pinIndex(w.from), b = pinIndex(w.to);
        if (a >= 0 && b >= 0)
            uf.unite(a, b);
    }

    // Number the nets in pin order (deterministic: part order, pin order).
    std::map<int, int> netOfRoot;
    for (std::size_t dev = 0; dev < nl.devices.size(); ++dev) {
        Device& d = nl.devices[dev];
        d.pinNets.resize(d.pinNames.size());
        for (std::size_t i = 0; i < d.pinNames.size(); ++i) {
            const int root = uf.find(globalPin[dev][i]);
            auto [it, inserted] = netOfRoot.emplace(root, int(nl.nets.size()));
            if (inserted)
                nl.nets.emplace_back();
            d.pinNets[i] = it->second;
            Net& n = nl.nets[size_t(it->second)];
            const NetPin np{int(dev), int(i)};
            n.pins.push_back(np);
            if (isDriver(d.pinDirs[i]))
                n.drivers.push_back(np);
            else if (d.pinDirs[i] == PinDir::In)
                n.loads.push_back(np);
        }
    }
    for (Net& n : nl.nets) {
        const NetPin& np = n.drivers.empty() ? n.pins.front() : n.drivers.front();
        const Device& d = nl.devices[size_t(np.device)];
        n.name = d.partId + ":" + d.pinNames[size_t(np.pin)];
    }
    return nl;
}

int Netlist::netOf(const PinRef& ref) const
{
    const int dev = deviceOf(ref.part);
    if (dev < 0)
        return -1;
    const Device& d = devices[size_t(dev)];
    for (std::size_t i = 0; i < d.pinNames.size(); ++i)
        if (d.pinNames[i] == ref.pin)
            return d.pinNets[i];
    return -1;
}

int Netlist::deviceOf(const std::string& partId) const
{
    auto it = m_deviceIndex.find(partId);
    return it == m_deviceIndex.end() ? -1 : it->second;
}

std::size_t Netlist::connectedNetCount() const
{
    std::size_t n = 0;
    for (const Net& net : nets)
        n += net.pins.size() >= 2;
    return n;
}

} // namespace chiply
