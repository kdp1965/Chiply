#include "core/Edit.h"

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

std::vector<std::pair<std::string, std::string>> remapIds(const std::vector<Part>& incoming,
                                                          const std::set<std::string>& usedIn)
{
    std::set<std::string> used = usedIn;
    std::vector<std::pair<std::string, std::string>> out;
    for (const Part& p : incoming) {
        std::string id;
        if (isAutoId(p.id)) {
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

std::vector<std::string> insertFragment(Document& doc, Fragment frag, double dx, double dy, int* dropped)
{
    const auto map = remapIds(frag.parts, usedIds(doc));
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
