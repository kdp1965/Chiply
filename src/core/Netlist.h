#pragma once
// Electrical connectivity of a document (PLAN.md 5.1, 6.1).
//
// Every pin of every part belongs to exactly one net. Nets are formed by
// wires, junctions, and pins a part connects internally (Wokwi convention:
// pins named "<prefix>.<n>" with the same prefix, e.g. pushbutton 1.l/1.r,
// 7-segment COM.1/COM.2, Pico GND.1..8). Switches, buttons and resistors do
// NOT merge nets: they are devices between nets, so the simulator can open
// and close them.
#include "core/Document.h"
#include "core/PartLibrary.h"

#include <map>
#include <string>
#include <vector>

namespace chiply {

struct NetPin {
    int device = -1; // index into Netlist::devices
    int pin = -1;    // index into that device's pins
};

struct Net {
    std::vector<NetPin> pins;
    std::vector<NetPin> drivers; // out / inout / power pins
    std::vector<NetPin> loads;   // in pins
    std::string name;            // readable: first driver "part:PIN", else first pin
};

struct Device {
    std::string partId;
    std::string type;
    const PartDef* def = nullptr;        // null for unknown part types
    std::vector<std::string> pinNames;   // def pin order (+ extra names seen on wires)
    std::vector<PinDir> pinDirs;
    std::vector<int> pinNets;            // net index per pin
    const Json* attrs = nullptr;         // the part's attrs (document must outlive the netlist)
};

struct Netlist {
    std::vector<Device> devices;
    std::vector<Net> nets;
    std::vector<std::string> warnings;

    // Net of a pin, -1 if the part or pin does not exist.
    int netOf(const PinRef& ref) const;
    int deviceOf(const std::string& partId) const;

    // Nets with at least two pins (i.e. something is actually connected).
    std::size_t connectedNetCount() const;

    static Netlist build(const Document& doc, const PartLibrary& lib);

private:
    std::map<std::string, int> m_deviceIndex;
};

} // namespace chiply
