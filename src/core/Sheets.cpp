#include "core/Sheets.h"

#include "core/Blocks.h"
#include "core/WokwiJson.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <regex>
#include <set>

namespace fs = std::filesystem;

namespace chiply {

namespace {

bool isIdentifier(const std::string& s)
{
    static const std::regex re("[A-Za-z_][A-Za-z0-9_]*");
    return std::regex_match(s, re);
}

// "fulladd.json", "fulladd.diagram.json" -> "fulladd"
std::string sheetNameOf(const fs::path& file)
{
    std::string stem = file.stem().string();
    const std::string diagram = ".diagram";
    if (stem.size() > diagram.size() && stem.compare(stem.size() - diagram.size(), diagram.size(), diagram) == 0)
        stem.resize(stem.size() - diagram.size());
    return stem;
}

bool isSidecar(const fs::path& file)
{
    const std::string n = file.filename().string();
    const std::string tail = ".chiply.json";
    return n.size() >= tail.size() && n.compare(n.size() - tail.size(), tail.size(), tail) == 0;
}

// Parts that are the chip (kept in instances): cells, constants, junctions,
// memories, custom blocks, other sheets, and unknown types (so the problem
// is reported, not hidden). Everything else is there to try the sheet out.
bool keptInInstance(const Part& p, const PartDef* def)
{
    if (!def)
        return true;
    if (def->block || def->memory || def->sheet || p.type == "wokwi-junction")
        return true;
    return def->verilog.is_object() && (def->verilog.contains("cell") || def->verilog.contains("constant"));
}

struct Expander {
    const PartLibrary& lib;
    Document& out;
    FlatInfo* info;
    std::vector<std::string> stack; // sheet names being expanded

    void expand(const Document& doc, const std::string& prefix, double dx, double dy, bool top, const std::string& folder)
    {
        std::set<std::string> dropped, instances, ports;
        if (!top) {
            // A VCC or GND that only fed dropped parts (a test switch) goes too.
            std::set<std::string> gone;
            for (const Part& p : doc.parts)
                if (!isPortType(p.type) && !keptInInstance(p, lib.find(p.type)))
                    gone.insert(p.id);
            for (const Part& p : doc.parts) {
                const PartDef* def = lib.find(p.type);
                if (!def || !def->verilog.is_object() || !def->verilog.contains("constant"))
                    continue;
                bool used = false;
                for (const Wire& w : doc.wires)
                    used |= (w.from.part == p.id && !gone.count(w.to.part)) || (w.to.part == p.id && !gone.count(w.from.part));
                if (!used)
                    dropped.insert(p.id);
            }
        }
        for (const Part& p : doc.parts) {
            const PartDef* def = lib.find(p.type);
            if (dropped.count(p.id))
                continue;
            if (def && def->sheet) {
                const SheetInfo& sheet = *def->sheet;
                if (std::find(stack.begin(), stack.end(), sheet.name) != stack.end()) {
                    std::string chain;
                    for (const std::string& s : stack)
                        chain += s + " > ";
                    throw FlattenError("sheet " + sheet.name + " contains itself (" + chain + sheet.name + ")");
                }
                instances.insert(p.id);
                const std::string inst = prefix + p.id;
                // One junction per port joins the nets outside and inside.
                double n = 0;
                for (const SheetPort& port : sheet.ports) {
                    Part j;
                    j.type = "wokwi-junction";
                    j.id = inst + "__" + port.name;
                    j.left = p.left + dx;
                    j.top = p.top + dy + (n += 1.0);
                    out.parts.push_back(j);
                    if (top && info)
                        info->pins[p.id + ":" + port.name] = PinRef{j.id, "J"};
                }
                stack.push_back(sheet.name);
                expand(sheet.doc, inst + "__", dx + p.left, dy + p.top, false, fs::path(sheet.path).parent_path().string());
                stack.pop_back();
                continue;
            }
            if (!top && isPortType(p.type)) {
                ports.insert(p.id); // its junction was made with the instance
                continue;
            }
            if (!top && !keptInInstance(p, def)) {
                dropped.insert(p.id);
                continue;
            }
            Part q = p;
            q.id = prefix + p.id;
            q.left += dx;
            q.top += dy;
            if (!top && def && def->memory && q.attrs.is_object() && q.attrs.contains("file") && q.attrs["file"].is_string()) {
                // A ROM file is found next to the sheet it is used in.
                const fs::path f(q.attrs["file"].get<std::string>());
                if (!f.empty() && f.is_relative() && !folder.empty())
                    q.attrs["file"] = (fs::path(folder) / f).lexically_normal().string();
            }
            out.parts.push_back(std::move(q));
        }
        for (const Wire& w : doc.wires) {
            if (dropped.count(w.from.part) || dropped.count(w.to.part))
                continue;
            auto map = [&](const PinRef& r) {
                if (instances.count(r.part))
                    return PinRef{prefix + r.part + "__" + r.pin, "J"};
                if (ports.count(r.part))
                    return PinRef{prefix + r.part, "J"};
                return PinRef{prefix + r.part, r.pin};
            };
            Wire f;
            f.from = map(w.from);
            f.to = map(w.to);
            f.color = w.color;
            if (top)
                f.path = w.path; // (inside instances the route means nothing)
            out.wires.push_back(std::move(f));
        }
    }
};

} // namespace

std::optional<SheetInfo> loadSheet(const std::string& path, std::string* error)
{
    auto fail = [&](const std::string& why) -> std::optional<SheetInfo> {
        if (error)
            *error = path + ": " + why;
        return std::nullopt;
    };
    SheetInfo s;
    s.path = fs::absolute(path).lexically_normal().string();
    s.name = sheetNameOf(fs::path(path));
    if (!isIdentifier(s.name))
        return fail("the file name must be usable as a name (letters, digits, _; not starting with a digit)");
    try {
        s.doc = loadWokwiFile(path).doc;
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    std::vector<const Part*> ins, outs;
    for (const Part& p : s.doc.parts) {
        if (p.type == "chiply-port-in")
            ins.push_back(&p);
        else if (p.type == "chiply-port-out")
            outs.push_back(&p);
    }
    if (ins.empty() && outs.empty())
        return fail("no Sheet input or Sheet output parts (a sheet needs ports)");
    auto byPlace = [](const Part* a, const Part* b) {
        if (a->top != b->top)
            return a->top < b->top;
        if (a->left != b->left)
            return a->left < b->left;
        return a->id < b->id;
    };
    std::stable_sort(ins.begin(), ins.end(), byPlace);
    std::stable_sort(outs.begin(), outs.end(), byPlace);
    std::set<std::string> names;
    for (const auto* group : {&ins, &outs})
        for (const Part* p : *group) {
            if (!isIdentifier(p->id))
                return fail("port \"" + p->id + "\" is not usable as a pin name");
            if (!names.insert(p->id).second)
                return fail("two ports are named " + p->id);
            s.ports.push_back({p->id, group == &ins});
        }
    return s;
}

PartDef sheetPartDef(const SheetInfo& sheet)
{
    std::vector<AutoPin> left, right;
    for (const SheetPort& p : sheet.ports)
        (p.input ? left : right).push_back({p.name, p.input ? PinDir::In : PinDir::Out, false});
    PartDef d = autoSymbolPart("chiply-sheet-" + sheet.name, sheet.name, left, right);
    d.category = "Sheets";
    d.prefix = sheet.name + "_";
    d.verilog = {{"sheet", sheet.name}};
    d.source = sheet.path;
    d.sheet = std::make_shared<const SheetInfo>(sheet);
    return d;
}

SheetScan scanSheets(const std::vector<std::string>& roots, PartLibrary& lib)
{
    SheetScan scan;
    std::set<std::string> seen;
    for (const std::string& root : roots) {
        std::error_code ec;
        if (root.empty() || !fs::is_directory(root, ec))
            continue;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(root, ec))
            if (e.is_regular_file() && e.path().extension() == ".json" && !isSidecar(e.path()))
                files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& f : files) {
            std::string why;
            auto s = loadSheet(f.string(), &why);
            if (!s) {
                scan.warnings.push_back(why);
                continue;
            }
            if (!seen.insert(s->name).second)
                continue; // hidden by the same name in an earlier root
            lib.addOrReplace(sheetPartDef(*s));
            scan.loaded.push_back(s->name);
        }
    }
    return scan;
}

std::string defaultUserSheetsDir()
{
    if (const char* env = std::getenv("CHIPLY_SHEETS"); env && *env)
        return env;
    const char* home = std::getenv("HOME");
#if defined(__APPLE__)
    return (fs::path(home ? home : "/tmp") / "Library" / "Application Support" / "Chiply" / "sheets").string();
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return (fs::path(xdg) / "chiply" / "sheets").string();
    return (fs::path(home ? home : "/tmp") / ".local" / "share" / "chiply" / "sheets").string();
#endif
}

std::vector<std::string> sheetRoots(const std::string& designPath)
{
    std::vector<std::string> roots;
    if (!designPath.empty())
        roots.push_back((fs::absolute(designPath).parent_path() / "sheets").string());
    roots.push_back(defaultUserSheetsDir());
    return roots;
}

bool usesSheets(const Document& doc, const PartLibrary& lib)
{
    for (const Part& p : doc.parts)
        if (p.type.rfind("chiply-sheet-", 0) == 0)
            if (const PartDef* d = lib.find(p.type); d && d->sheet)
                return true;
    return false;
}

Document flattenSheets(const Document& doc, const PartLibrary& lib, FlatInfo* info)
{
    if (info)
        info->pins.clear();
    if (!usesSheets(doc, lib))
        return doc;
    Document out = doc;
    out.parts.clear();
    out.wires.clear();
    Expander e{lib, out, info, {}};
    e.expand(doc, "", 0, 0, true, "");
    return out;
}

std::string sheetProblem(const SheetInfo& sheet, const PartLibrary& lib)
{
    std::vector<std::string> stack{sheet.name};
    std::function<std::string(const SheetInfo&)> walk = [&](const SheetInfo& s) -> std::string {
        for (const Part& p : s.doc.parts) {
            const PartDef* def = lib.find(p.type);
            if (!def)
                return "sheet " + s.name + " has a part of unknown type " + p.type + " (" + p.id + ")";
            if (!def->sheet)
                continue;
            const SheetInfo& inner = *def->sheet;
            if (std::find(stack.begin(), stack.end(), inner.name) != stack.end())
                return "sheet " + inner.name + " contains itself";
            stack.push_back(inner.name);
            const std::string r = walk(inner);
            stack.pop_back();
            if (!r.empty())
                return r;
        }
        return {};
    };
    return walk(sheet);
}

} // namespace chiply
