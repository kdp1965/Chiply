#pragma once
// Part definitions: outline size, pins (position + electrical direction),
// symbol name for the renderer, and the Verilog cell mapping. Loaded from
// resources/parts.json, which is compiled into the core library.
#include "core/Json.h"

#include <deque>
#include <map>
#include <memory>
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

// A custom block (PLAN.md 7.2): a Verilog module behind a symbol, loaded
// from <folder>/block.json.
struct BlockPort {
    std::string name;
    PinDir dir = PinDir::In;
    int width = 1;   // >1: one pin per bit, name0..name<width-1>
    bool clock = false;
};
struct BlockInfo {
    std::string name;     // part type "chiply-block-<name>"
    std::string module;   // Verilog module
    std::string label;    // palette / Inspector name (default: name)
    std::string prefix;   // id prefix for new parts (default: name + "_")
    std::string folder;   // absolute
    std::vector<std::string> verilog; // absolute paths
    std::vector<BlockPort> ports;
    Json params = Json::object(); // name -> default (number or string)
    // Pin name of bit `bit` of a port (the port name itself when 1 bit wide).
    static std::string pinName(const BlockPort& p, int bit)
    {
        return p.width == 1 ? p.name : p.name + std::to_string(bit);
    }
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
    std::shared_ptr<const BlockInfo> block; // custom blocks only

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
    // A deque, so definitions never move: PartDef pointers held by netlists
    // and scene items stay valid when blocks are added.
    const std::deque<PartDef>& parts() const { return m_parts; }
    // Adds a part type, or replaces the definition of one with the same
    // type in place (custom blocks being reloaded).
    const PartDef& addOrReplace(PartDef def);

    // The library compiled into Chiply (resources/parts.json), plus the
    // custom blocks found so far (core/Blocks).
    static const PartLibrary& builtin();
    static PartLibrary& global();

    static constexpr double kPxPerMm = 96.0 / 25.4;

private:
    std::deque<PartDef> m_parts;
    std::map<std::string, std::size_t> m_index;
};

} // namespace chiply
