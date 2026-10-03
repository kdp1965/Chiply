#pragma once
// In-memory model of a Wokwi diagram.json. Everything Chiply does not
// interpret (unknown top-level keys, unknown part keys, extra connection
// elements, unparseable wire paths) is kept verbatim so a load/save cycle is
// lossless.
#include "core/Json.h"
#include "core/WirePath.h"

#include <optional>
#include <string>
#include <vector>

namespace chiply {

struct PinRef {
    std::string part;
    std::string pin;

    static std::optional<PinRef> parse(const std::string& ref); // "part:PIN"
    std::string str() const { return part + ":" + pin; }
    bool operator==(const PinRef&) const = default;
};

struct Part {
    std::string type;
    std::string id;
    double top = 0;
    double left = 0;
    int rotate = 0;
    Json attrs = Json::object();
    std::optional<bool> hide;

    // Fidelity bookkeeping.
    bool hasRotateKey = false;           // write "rotate" even when 0
    bool hasAttrsKey = true;             // Wokwi always writes attrs
    std::vector<std::string> keyOrder;   // original key order; empty = canonical
    Json extra = Json::object();         // keys Chiply does not know
};

struct Wire {
    PinRef from;
    PinRef to;
    std::string color = "green";
    WirePath path;

    // Fidelity bookkeeping.
    bool hasPathElement = true;          // 4th array element present
    std::optional<Json> rawPath;         // set when the path could not be parsed
    Json extraElements = Json::array();  // array elements beyond the 4th
};

struct Document {
    std::vector<Part> parts;
    std::vector<Wire> wires;

    // Top-level object with "parts" and "connections" replaced by null
    // placeholders, so their position and every other key (version, author,
    // editor, dependencies, serialMonitor, ...) survive unchanged.
    Json root = Json::object();
    bool trailingNewline = false; // Wokwi writes none; keep whatever was loaded

    std::string author() const;
    void setAuthor(const std::string& a);

    Part* findPart(const std::string& id);
    const Part* findPart(const std::string& id) const;

    // Renames a part and rewrites every wire end that references it.
    // Returns false if `from` does not exist or `to` is already taken.
    bool renamePart(const std::string& from, const std::string& to);

    static Document makeEmpty(const std::string& author = {});
};

} // namespace chiply
