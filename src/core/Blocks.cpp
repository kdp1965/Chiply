#include "core/Blocks.h"

#include "core/Document.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace chiply {

namespace {

bool isIdentifier(const std::string& s)
{
    static const std::regex re("[A-Za-z_][A-Za-z0-9_]*");
    return std::regex_match(s, re);
}

constexpr double kGrid = 9.6; // 0.1 inch in px

} // namespace

std::optional<BlockInfo> loadBlock(const std::string& folder, std::string* error)
{
    auto fail = [&](const std::string& why) -> std::optional<BlockInfo> {
        if (error)
            *error = (fs::path(folder) / "block.json").string() + ": " + why;
        return std::nullopt;
    };
    std::ifstream in(fs::path(folder) / "block.json", std::ios::binary);
    if (!in)
        return fail("cannot read");
    std::stringstream ss;
    ss << in.rdbuf();
    Json j;
    try {
        j = Json::parse(ss.str());
    } catch (const std::exception& e) {
        return fail(std::string("not valid JSON: ") + e.what());
    }
    if (!j.is_object())
        return fail("expected an object");
    BlockInfo b;
    b.folder = fs::absolute(folder).lexically_normal().string();
    b.name = j.value("name", std::string());
    if (!isIdentifier(b.name))
        return fail("\"name\" must be a Verilog identifier");
    b.module = j.value("module", b.name);
    if (!isIdentifier(b.module))
        return fail("\"module\" must be a Verilog identifier");
    if (j.contains("verilog")) {
        if (!j["verilog"].is_array())
            return fail("\"verilog\" must be a list of files");
        for (const Json& f : j["verilog"]) {
            if (!f.is_string())
                return fail("\"verilog\" must be a list of files");
            const fs::path p = fs::path(b.folder) / f.get<std::string>();
            if (!fs::exists(p))
                return fail("Verilog file " + f.get<std::string>() + " not found");
            b.verilog.push_back(p.lexically_normal().string());
        }
    }
    if (!j.contains("ports") || !j["ports"].is_array() || j["ports"].empty())
        return fail("\"ports\" must list the block's ports");
    std::set<std::string> pinNames;
    for (const Json& pj : j["ports"]) {
        if (!pj.is_object())
            return fail("each port must be an object");
        BlockPort p;
        p.name = pj.value("name", std::string());
        if (!isIdentifier(p.name))
            return fail("port name \"" + p.name + "\" is not a Verilog identifier");
        const std::string dir = pj.value("dir", std::string("in"));
        if (dir == "in" || dir == "input")
            p.dir = PinDir::In;
        else if (dir == "out" || dir == "output")
            p.dir = PinDir::Out;
        else if (dir == "inout")
            p.dir = PinDir::InOut;
        else
            return fail("port " + p.name + ": dir must be in, out or inout");
        p.width = pj.value("width", 1);
        if (p.width < 1 || p.width > 64)
            return fail("port " + p.name + ": width must be 1..64");
        p.clock = pj.value("clock", false);
        for (int bit = 0; bit < p.width; ++bit)
            if (!pinNames.insert(BlockInfo::pinName(p, bit)).second)
                return fail("pin " + BlockInfo::pinName(p, bit) + " appears twice");
        b.ports.push_back(p);
    }
    if (j.contains("params")) {
        if (!j["params"].is_object())
            return fail("\"params\" must be an object");
        for (const auto& [k, v] : j["params"].items()) {
            if (!isIdentifier(k) || !(v.is_number() || v.is_string()))
                return fail("parameter " + k + " must be a number or a string");
        }
        b.params = j["params"];
    }
    b.label = j.contains("label") && j["label"].is_string() ? j["label"].get<std::string>() : b.name;
    b.prefix = b.name + "_";
    if (j.contains("prefix") && j["prefix"].is_string() && isIdentifier(j["prefix"].get<std::string>()))
        b.prefix = j["prefix"].get<std::string>();
    return b;
}

PartDef blockPartDef(const BlockInfo& block)
{
    const BlockInfo& b = block;
    const std::string& label = b.label;
    const std::string& prefix = b.prefix;
    std::vector<std::pair<std::string, const BlockPort*>> left, right;
    for (const BlockPort& p : b.ports)
        for (int bit = 0; bit < p.width; ++bit)
            (p.dir == PinDir::In ? left : right).push_back({BlockInfo::pinName(p, bit), &p});
    std::size_t maxL = 0, maxR = 0;
    for (const auto& [n, p] : left)
        maxL = std::max(maxL, n.size());
    for (const auto& [n, p] : right)
        maxR = std::max(maxR, n.size());
    // Labels are drawn about 0.65 grid high (~3.7 px per character).
    const double inner = std::max({6 * kGrid, double(maxL + maxR) * 3.9 + 2 * kGrid, double(label.size()) * 4.6 + kGrid});
    const double w = (std::ceil(inner / kGrid) + 4) * kGrid; // whole grid steps
    const std::size_t rows = std::max(left.size(), right.size());
    const double h = double(rows + 2) * kGrid;

    PartDef d;
    d.type = "chiply-block-" + b.name;
    d.label = label;
    d.category = "Custom";
    d.prefix = prefix;
    d.symbol = "block";
    d.width = w;
    d.height = h;
    for (std::size_t i = 0; i < left.size(); ++i) {
        PinDef p;
        p.name = left[i].first;
        p.x = 0;
        p.y = double(i + 2) * kGrid;
        p.dir = PinDir::In;
        p.clock = left[i].second->clock;
        d.pins.push_back(p);
    }
    for (std::size_t i = 0; i < right.size(); ++i) {
        PinDef p;
        p.name = right[i].first;
        p.x = w;
        p.y = double(i + 2) * kGrid;
        p.dir = right[i].second->dir;
        p.clock = right[i].second->clock;
        d.pins.push_back(p);
    }
    d.verilog = {{"block", b.module}};
    Json attrs = Json::object();
    for (const auto& [k, v] : b.params.items())
        attrs[k] = v.is_string() ? v.get<std::string>() : v.dump();
    d.attrs = attrs;
    d.source = b.folder;
    d.block = std::make_shared<const BlockInfo>(b);
    return d;
}

BlockScan scanBlocks(const std::vector<std::string>& roots, PartLibrary& lib)
{
    BlockScan scan;
    std::set<std::string> seen;
    for (const std::string& root : roots) {
        std::error_code ec;
        if (root.empty() || !fs::is_directory(root, ec))
            continue;
        std::vector<fs::path> folders;
        for (const auto& e : fs::directory_iterator(root, ec))
            if (e.is_directory() && fs::exists(e.path() / "block.json"))
                folders.push_back(e.path());
        std::sort(folders.begin(), folders.end());
        for (const fs::path& f : folders) {
            std::string why;
            auto b = loadBlock(f.string(), &why);
            if (!b) {
                scan.warnings.push_back(why);
                continue;
            }
            if (!seen.insert(b->name).second)
                continue; // hidden by the same name in an earlier root
            lib.addOrReplace(blockPartDef(*b));
            scan.loaded.push_back(b->name);
        }
    }
    return scan;
}

std::string defaultUserBlocksDir()
{
    if (const char* env = std::getenv("CHIPLY_BLOCKS"); env && *env)
        return env;
    const char* home = std::getenv("HOME");
#if defined(__APPLE__)
    return (fs::path(home ? home : "/tmp") / "Library" / "Application Support" / "Chiply" / "blocks").string();
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return (fs::path(xdg) / "chiply" / "blocks").string();
    return (fs::path(home ? home : "/tmp") / ".local" / "share" / "chiply" / "blocks").string();
#endif
}

std::vector<std::string> blockRoots(const std::string& designPath)
{
    std::vector<std::string> roots;
    if (!designPath.empty())
        roots.push_back((fs::absolute(designPath).parent_path() / "blocks").string());
    roots.push_back(defaultUserBlocksDir());
    return roots;
}

std::vector<std::string> blockSources(const Document& doc, const PartLibrary& lib)
{
    std::vector<std::string> files;
    std::set<std::string> types;
    for (const Part& p : doc.parts) {
        if (!types.insert(p.type).second)
            continue;
        const PartDef* d = lib.find(p.type);
        if (d && d->block)
            for (const std::string& f : d->block->verilog)
                if (std::find(files.begin(), files.end(), f) == files.end())
                    files.push_back(f);
    }
    return files;
}

} // namespace chiply
