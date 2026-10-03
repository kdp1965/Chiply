#pragma once
// Part definitions: outline size, pins (position + electrical direction),
// symbol name for the renderer, and the Verilog cell mapping. Loaded from
// resources/parts.json, which is compiled into the core library.
#include "core/Json.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace chiply {

enum class PinDir { In, Out, InOut, Power, Passive };

std::optional<PinDir> parsePinDir(const std::string& s);
const char* pinDirName(PinDir d);

struct PinDef {
    std::string name;
    double x = 0;      // px, relative to the unrotated top-left corner
    double y = 0;
    PinDir dir = PinDir::Passive;
    bool clock = false;
    std::string signal; // "vcc" / "gnd" for power pins (default wire color)
};

struct PartDef {
    std::string type;
    std::string label;
    std::string category;
    std::string prefix;
    std::string symbol;  // renderer key, e.g. "and", "dff-sr", "tt-input"
    double width = 0;    // px
    double height = 0;
    std::vector<PinDef> pins;
    Json verilog = Json::object(); // {cell, ports{PIN: port}} or {constant}
    Json attrs = Json::object();   // defaults for new parts
    std::string source;

    const PinDef* findPin(const std::string& name) const;
};

// Chiply's own parts (type "chiply-..."), offered only in Extended mode
// (PLAN.md 7.1): Wokwi cannot load a design that uses them.
inline bool isExtensionType(const std::string& type) { return type.rfind("chiply-", 0) == 0; }

class PartLibrary {
public:
    // Throws std::runtime_error on malformed input.
    void loadJson(const std::string& text);

    const PartDef* find(const std::string& type) const;
    const std::vector<PartDef>& parts() const { return m_parts; }

    // The library compiled into Chiply (resources/parts.json).
    static const PartLibrary& builtin();

    static constexpr double kPxPerMm = 96.0 / 25.4;

private:
    std::vector<PartDef> m_parts;
    std::map<std::string, std::size_t> m_index;
};

} // namespace chiply
