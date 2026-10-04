#include "core/PartLibrary.h"

#include "core/Memory.h"

#include <mutex>

#include <stdexcept>

namespace chiply {

extern const char* const kBuiltinPartsJson; // generated from resources/parts.json

std::optional<PinDir> parsePinDir(const std::string& s)
{
    if (s == "in") return PinDir::In;
    if (s == "out") return PinDir::Out;
    if (s == "inout") return PinDir::InOut;
    if (s == "power") return PinDir::Power;
    if (s == "passive") return PinDir::Passive;
    return std::nullopt;
}

const char* pinDirName(PinDir d)
{
    switch (d) {
    case PinDir::In: return "input";
    case PinDir::Out: return "output";
    case PinDir::InOut: return "bidirectional";
    case PinDir::Power: return "power";
    case PinDir::Passive: return "passive";
    }
    return "?";
}

const PinDef* PartDef::findPin(const std::string& name) const
{
    for (const PinDef& p : pins)
        if (p.name == name)
            return &p;
    return nullptr;
}

void PartLibrary::loadJson(const std::string& text)
{
    Json root = Json::parse(text);
    for (const Json& j : root.at("parts")) {
        PartDef d;
        d.type = j.at("type").get<std::string>();
        d.label = j.value("label", d.type);
        d.category = j.value("category", "Misc");
        d.prefix = j.value("prefix", "part");
        d.symbol = j.value("symbol", "box");
        d.source = j.value("source", "");
        const std::string units = j.value("units", "px");
        double scale = 1.0;
        if (units == "mm")
            scale = kPxPerMm;
        else if (units != "px")
            throw std::runtime_error(d.type + ": unknown units \"" + units + "\"");
        const Json& size = j.at("size");
        d.width = size.at(0).get<double>() * scale;
        d.height = size.at(1).get<double>() * scale;
        for (const Json& pj : j.at("pins")) {
            PinDef p;
            p.name = pj.at("name").get<std::string>();
            p.x = pj.at("x").get<double>() * scale;
            p.y = pj.at("y").get<double>() * scale;
            auto dir = parsePinDir(pj.value("dir", "passive"));
            if (!dir)
                throw std::runtime_error(d.type + ":" + p.name + ": bad pin direction");
            p.dir = *dir;
            p.clock = pj.value("clock", false);
            p.signal = pj.value("signal", "");
            d.pins.push_back(std::move(p));
        }
        if (j.contains("verilog"))
            d.verilog = j.at("verilog");
        if (j.contains("attrs"))
            d.attrs = j.at("attrs");
        if (m_index.count(d.type))
            throw std::runtime_error("duplicate part type " + d.type);
        m_index[d.type] = m_parts.size();
        m_parts.push_back(std::move(d));
    }
}

namespace {
// Guards lookups against memory types being added on first use.
std::recursive_mutex& libraryMutex()
{
    static std::recursive_mutex m;
    return m;
}
} // namespace

const PartDef* PartLibrary::find(const std::string& type) const
{
    std::lock_guard<std::recursive_mutex> lock(libraryMutex());
    auto it = m_index.find(type);
    if (it != m_index.end())
        return &m_parts[it->second];
    // RAM / ROM of any supported size: made on first use (PLAN.md 7.3).
    if (auto mem = parseMemoryType(type)) {
        PartDef d = memoryPartDef(*mem);
        d.hidden = true; // Add Part offers the default sizes only
        auto* self = const_cast<PartLibrary*>(this);
        return &self->addOrReplace(std::move(d));
    }
    return nullptr;
}

const PartDef& PartLibrary::addOrReplace(PartDef def)
{
    std::lock_guard<std::recursive_mutex> lock(libraryMutex());
    auto it = m_index.find(def.type);
    if (it != m_index.end()) {
        m_parts[it->second] = std::move(def);
        return m_parts[it->second];
    }
    m_index[def.type] = m_parts.size();
    m_parts.push_back(std::move(def));
    return m_parts.back();
}

PartLibrary& PartLibrary::global()
{
    static PartLibrary lib = [] {
        PartLibrary l;
        l.loadJson(kBuiltinPartsJson);
        // Memories offered in Add Part; other sizes are made on first use.
        l.addOrReplace(memoryPartDef(*parseMemoryType(memoryType(false, 16, 8))));
        l.addOrReplace(memoryPartDef(*parseMemoryType(memoryType(true, 16, 8))));
        return l;
    }();
    return lib;
}

const PartLibrary& PartLibrary::builtin() { return global(); }

} // namespace chiply
