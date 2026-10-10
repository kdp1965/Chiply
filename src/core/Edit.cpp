#include "core/Edit.h"

#include "core/Geometry.h"
#include "core/PartLibrary.h"

#include <cmath>

#include <regex>

#include <cctype>

#include "core/IdGen.h"
#include "core/JsonFormat.h"
#include "core/WokwiJson.h"

#include <algorithm>

namespace chiply {

std::set<std::string> usedIds(const Document& doc)
{
    std::set<std::string> ids;
    for (const Part& p : doc.parts)
        ids.insert(p.id);
    return ids;
}

NameFormat::NameFormat(const std::string& format)
{
    if (format.empty())
        return;
    m_active = true;
    int hashes = 0;
    std::string re = "^";
    for (char c : format) {
        if (c == '#') {
            ++hashes;
            re += "([0-9]+)";
        } else if (c == '*') {
            re += ".*";
        } else if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            re += c;
        } else {
            re += std::string("\\") + c;
        }
    }
    if (hashes != 1) {
        m_error = "the format needs exactly one # (the number to step)";
        m_active = false;
        return;
    }
    m_regex = re + "$";
}

std::optional<std::string> NameFormat::step(const std::string& id, long by) const
{
    if (!m_active)
        return std::nullopt;
    const std::regex re(m_regex);
    std::smatch m;
    if (!std::regex_match(id, m, re))
        return std::nullopt;
    const std::string digits = m[1].str();
    long n = 0;
    try {
        n = std::stol(digits);
    } catch (...) {
        return std::nullopt;
    }
    std::string next = std::to_string(n + by);
    if (digits.size() > 1 && digits[0] == '0' && next.size() < digits.size())
        next.insert(0, digits.size() - next.size(), '0'); // keep zero padding
    return id.substr(0, std::size_t(m.position(1))) + next + id.substr(std::size_t(m.position(1) + m.length(1)));
}

std::vector<std::pair<std::string, std::string>> remapIds(const std::vector<Part>& incoming,
                                                          const std::set<std::string>& usedIn,
                                                          const NameFormat& format)
{
    std::set<std::string> used = usedIn;
    // Paste name format: one step for the whole group, the smallest that
    // frees every matching id (and keeps them distinct).
    std::map<std::size_t, std::string> stepped;
    if (format.active()) {
        for (long k = 1; k < 100000; ++k) {
            std::map<std::size_t, std::string> trial;
            std::set<std::string> fresh;
            bool ok = true;
            for (std::size_t i = 0; i < incoming.size() && ok; ++i)
                if (auto n = format.step(incoming[i].id, k)) {
                    ok = !used.count(*n) && fresh.insert(*n).second;
                    trial[i] = *n;
                }
            if (ok) {
                stepped = std::move(trial);
                break;
            }
        }
        for (const auto& [i, id] : stepped)
            used.insert(id);
    }
    std::vector<std::pair<std::string, std::string>> out;
    for (std::size_t i = 0; i < incoming.size(); ++i) {
        const Part& p = incoming[i];
        std::string id;
        if (auto st = stepped.find(i); st != stepped.end()) {
            id = st->second;
        } else if (isAutoId(p.id)) {
            id = nextFreeId(splitTrailingNumber(p.id).first, used);
        } else if (!used.count(p.id)) {
            id = p.id;
        } else {
            for (int n = 1;; ++n) {
                std::string c = p.id + "_" + std::to_string(n);
                if (!used.count(c)) {
                    id = c;
                    break;
                }
            }
        }
        used.insert(id);
        out.emplace_back(p.id, id);
    }
    return out;
}

Fragment extractFragment(const Document& doc, const std::set<std::string>& ids)
{
    Fragment f;
    for (const Part& p : doc.parts)
        if (ids.count(p.id))
            f.parts.push_back(p);
    for (const Wire& w : doc.wires)
        if (ids.count(w.from.part) && ids.count(w.to.part))
            f.wires.push_back(w);
    return f;
}

namespace {
constexpr const char* kEndAttr = "end";
constexpr const char* kJunction = "wokwi-junction";
} // namespace

bool isEndPlaceholder(const Part& p)
{
    return p.type == kJunction && p.attrs.is_object() && p.attrs.contains(kEndAttr);
}

Fragment extractFragment(const Document& doc, const PartLibrary& lib, const std::set<std::string>& ids,
                         const std::set<std::size_t>& wireIndices)
{
    Fragment f = extractFragment(doc, ids);
    const PartDef* jdef = lib.find(kJunction);
    std::map<std::string, std::string> placeholders; // "part:pin" -> placeholder id
    std::set<std::string> taken = ids;
    int n = 0;
    // The wire end as it goes on the clipboard: itself inside the selection,
    // otherwise the placeholder standing on its pin (one per pin).
    auto endFor = [&](const PinRef& ref) -> std::optional<PinRef> {
        if (ids.count(ref.part))
            return ref;
        if (auto it = placeholders.find(ref.str()); it != placeholders.end())
            return PinRef{it->second, "J"};
        const std::optional<Point> at = pinPosition(doc, lib, ref);
        if (!at || !jdef || jdef->pins.empty())
            return std::nullopt;
        std::string id;
        do
            id = "end" + std::to_string(++n);
        while (taken.count(id));
        taken.insert(id);
        Part j;
        j.type = kJunction;
        j.id = id;
        j.left = round2(at->x - jdef->pins.front().x);
        j.top = round2(at->y - jdef->pins.front().y);
        j.attrs = Json{{kEndAttr, ref.str()}};
        f.parts.push_back(std::move(j));
        placeholders.emplace(ref.str(), id);
        return PinRef{id, "J"};
    };
    for (std::size_t i : wireIndices) {
        if (i >= doc.wires.size())
            continue;
        const Wire& w = doc.wires[i];
        if (ids.count(w.from.part) && ids.count(w.to.part))
            continue; // already in
        const auto a = endFor(w.from), b = endFor(w.to);
        if (!a || !b)
            continue;
        Wire c = w;
        c.from = *a;
        c.to = *b;
        f.wires.push_back(std::move(c));
    }
    return f;
}

EndResolution resolveEnds(Document& doc, const PartLibrary& lib, const std::vector<std::string>& ids, double tolerance)
{
    EndResolution r;
    struct Candidate {
        PinRef ref;
        Point at;
    };
    std::vector<Candidate> candidates;
    for (const Part& p : doc.parts) {
        if (isEndPlaceholder(p))
            continue;
        const PartDef* def = lib.find(p.type);
        if (!def)
            continue;
        for (const PinDef& pin : def->pins)
            if (const auto at = pinPosition(p, *def, pin.name))
                candidates.push_back({PinRef{p.id, pin.name}, *at});
    }
    std::map<std::string, PinRef> rewire; // placeholder id -> the pin it landed on
    for (const std::string& id : ids) {
        Part* p = doc.findPart(id);
        if (!p || !isEndPlaceholder(*p))
            continue;
        std::string wantPin;
        if (p->attrs[kEndAttr].is_string()) {
            const std::string s = p->attrs[kEndAttr].get<std::string>();
            if (const auto c = s.find(':'); c != std::string::npos)
                wantPin = s.substr(c + 1);
        }
        const Candidate* best = nullptr;
        double bestD = 0;
        bool bestNamed = false;
        if (const auto at = pinPosition(doc, lib, PinRef{id, "J"}))
            for (const Candidate& c : candidates) {
                const double d = std::hypot(c.at.x - at->x, c.at.y - at->y);
                if (d > tolerance)
                    continue;
                const bool named = c.ref.pin == wantPin;
                if (!best || (named && !bestNamed) || (named == bestNamed && d < bestD)) {
                    best = &c;
                    bestD = d;
                    bestNamed = named;
                }
            }
        if (best) {
            rewire.emplace(id, best->ref);
            ++r.connected;
        } else {
            p->attrs.erase(kEndAttr); // an ordinary junction from now on
            ++r.left;
        }
    }
    if (rewire.empty())
        return r;
    for (Wire& w : doc.wires) {
        if (auto it = rewire.find(w.from.part); it != rewire.end() && w.from.pin == "J")
            w.from = it->second;
        if (auto it = rewire.find(w.to.part); it != rewire.end() && w.to.pin == "J")
            w.to = it->second;
    }
    std::erase_if(doc.parts, [&](const Part& p) { return rewire.count(p.id) != 0; });
    return r;
}

std::vector<std::string> insertFragment(Document& doc, Fragment frag, double dx, double dy, int* dropped,
                                        const NameFormat& format)
{
    const auto map = remapIds(frag.parts, usedIds(doc), format);
    // Several incoming parts can share an old id (hand-edited JSON); wires
    // follow the first one, which matches how Wokwi resolves ids.
    std::map<std::string, std::string> lookup;
    for (const auto& [from, to] : map)
        lookup.emplace(from, to);
    std::vector<std::string> newIds;
    for (std::size_t i = 0; i < frag.parts.size(); ++i) {
        Part p = frag.parts[i];
        p.id = map[i].second;
        p.left += dx;
        p.top += dy;
        newIds.push_back(p.id);
        doc.parts.push_back(std::move(p));
    }
    int drop = 0;
    for (Wire w : frag.wires) {
        auto a = lookup.find(w.from.part), b = lookup.find(w.to.part);
        if (a == lookup.end() || b == lookup.end()) {
            ++drop;
            continue;
        }
        w.from.part = a->second;
        w.to.part = b->second;
        doc.wires.push_back(std::move(w));
    }
    if (dropped)
        *dropped = drop;
    return newIds;
}

std::map<std::string, std::string> replaceInIds(const Document& doc, const std::vector<std::string>& ids,
                                                const std::string& from, const std::string& to, std::string* error)
{
    std::map<std::string, std::string> out;
    if (from.empty())
        return out;
    const std::set<std::string> renamedSet(ids.begin(), ids.end());
    std::set<std::string> taken;
    for (const Part& p : doc.parts)
        if (!renamedSet.count(p.id))
            taken.insert(p.id); // parts that keep their ids
    std::set<std::string> result;
    for (const std::string& id : ids) {
        std::string n = id;
        for (std::size_t pos = 0; (pos = n.find(from, pos)) != std::string::npos; pos += to.size())
            n.replace(pos, from.size(), to);
        if (n.empty()) {
            if (error)
                *error = id + " would get an empty name";
            return {};
        }
        if (taken.count(n) || !result.insert(n).second) {
            if (error)
                *error = id + " would become " + n + ", which is already used";
            return {};
        }
        if (n != id)
            out[id] = n;
    }
    return out;
}

void renameParts(Document& doc, const std::map<std::string, std::string>& rename)
{
    for (Part& p : doc.parts)
        if (auto it = rename.find(p.id); it != rename.end())
            p.id = it->second;
    for (Wire& w : doc.wires) {
        if (auto it = rename.find(w.from.part); it != rename.end())
            w.from.part = it->second;
        if (auto it = rename.find(w.to.part); it != rename.end())
            w.to.part = it->second;
    }
}

Removed removeItems(Document& doc, const std::set<std::string>& partIds, const std::set<std::size_t>& wireIdx)
{
    Removed r;
    std::vector<Wire> keepW;
    for (std::size_t i = 0; i < doc.wires.size(); ++i) {
        const Wire& w = doc.wires[i];
        if (wireIdx.count(i) || partIds.count(w.from.part) || partIds.count(w.to.part))
            r.wires.emplace_back(i, w);
        else
            keepW.push_back(w);
    }
    std::vector<Part> keepP;
    for (std::size_t i = 0; i < doc.parts.size(); ++i) {
        if (partIds.count(doc.parts[i].id))
            r.parts.emplace_back(i, doc.parts[i]);
        else
            keepP.push_back(doc.parts[i]);
    }
    doc.wires = std::move(keepW);
    doc.parts = std::move(keepP);
    return r;
}

void restoreItems(Document& doc, const Removed& r)
{
    // Ascending original indices: each insert lands exactly where it was.
    for (const auto& [i, p] : r.parts)
        doc.parts.insert(doc.parts.begin() + static_cast<long>(std::min(i, doc.parts.size())), p);
    for (const auto& [i, w] : r.wires)
        doc.wires.insert(doc.wires.begin() + static_cast<long>(std::min(i, doc.wires.size())), w);
}

void removeLast(Document& doc, std::size_t parts, std::size_t wires)
{
    doc.parts.resize(doc.parts.size() - std::min(parts, doc.parts.size()));
    doc.wires.resize(doc.wires.size() - std::min(wires, doc.wires.size()));
}

std::string fragmentToText(const Fragment& f)
{
    Json root = Json::object();
    Json parts = Json::array();
    for (const Part& p : f.parts)
        parts.push_back(partToJson(p));
    Json conns = Json::array();
    for (const Wire& w : f.wires)
        conns.push_back(wireToJson(w));
    root["parts"] = std::move(parts);
    root["connections"] = std::move(conns);
    return prettyPrint(root) + "\n";
}

std::optional<Fragment> fragmentFromText(const std::string& text)
{
    try {
        LoadResult r = loadWokwi(text);
        if (r.doc.parts.empty())
            return std::nullopt;
        return Fragment{std::move(r.doc.parts), std::move(r.doc.wires)};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

Point fragmentOrigin(const Fragment& f)
{
    if (f.parts.empty())
        return {};
    Point o{f.parts.front().left, f.parts.front().top};
    for (const Part& p : f.parts) {
        o.x = std::min(o.x, p.left);
        o.y = std::min(o.y, p.top);
    }
    return o;
}

} // namespace chiply
