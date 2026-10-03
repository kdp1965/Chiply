#include "core/WokwiJson.h"

#include "core/JsonFormat.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace chiply {

namespace {

const char* const kCanonicalPartKeys[] = {"type", "id", "top", "left", "rotate", "hide", "attrs"};

double numberOr(const Json& j, double fallback)
{
    return j.is_number() ? j.get<double>() : fallback;
}

// Integral doubles are stored as JSON integers so they print without ".0".
Json numberJson(double v)
{
    return Json::parse(formatNumber(v));
}

} // namespace

Part partFromJson(const Json& j)
{
    if (!j.is_object())
        throw LoadError("part is not an object");
    Part p;
    p.hasAttrsKey = false;
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& k = it.key();
        const Json& v = it.value();
        p.keyOrder.push_back(k);
        if (k == "type" && v.is_string())
            p.type = v.get<std::string>();
        else if (k == "id" && v.is_string())
            p.id = v.get<std::string>();
        else if (k == "top" && v.is_number())
            p.top = v.get<double>();
        else if (k == "left" && v.is_number())
            p.left = v.get<double>();
        else if (k == "rotate" && v.is_number()) {
            p.rotate = static_cast<int>(numberOr(v, 0));
            p.hasRotateKey = true;
        } else if (k == "hide" && v.is_boolean())
            p.hide = v.get<bool>();
        else if (k == "attrs" && v.is_object()) {
            p.attrs = v;
            p.hasAttrsKey = true;
        } else {
            p.extra[k] = v;
            // Keep it in keyOrder; partToJson pulls it from `extra`.
        }
    }
    if (p.type.empty() || p.id.empty())
        throw LoadError("part is missing \"type\" or \"id\"");
    return p;
}

Json partToJson(const Part& p)
{
    Json j = Json::object();
    auto put = [&](const std::string& k) {
        if (k == "type")
            j["type"] = p.type;
        else if (k == "id")
            j["id"] = p.id;
        else if (k == "top")
            j["top"] = numberJson(p.top);
        else if (k == "left")
            j["left"] = numberJson(p.left);
        else if (k == "rotate") {
            if (p.rotate != 0 || p.hasRotateKey)
                j["rotate"] = p.rotate;
        } else if (k == "hide") {
            if (p.hide)
                j["hide"] = *p.hide;
        } else if (k == "attrs") {
            if (p.hasAttrsKey || !p.attrs.empty())
                j["attrs"] = p.attrs;
        } else if (p.extra.contains(k)) {
            j[k] = p.extra.at(k);
        }
    };
    for (const std::string& k : p.keyOrder)
        put(k);
    // Keys not in the original order (new parts, or rotate added by an edit)
    // go in Wokwi's canonical position relative to what is already there.
    // For a loaded part, a key it did not have is only added once it carries
    // information (Wokwi omits top/left when 0 in some hand-written files).
    const bool loaded = !p.keyOrder.empty();
    for (const char* k : kCanonicalPartKeys) {
        if (j.contains(k))
            continue;
        std::string key = k;
        if (loaded && ((key == "top" && p.top == 0) || (key == "left" && p.left == 0)))
            continue;
        put(key);
    }
    if (j.contains("rotate") && !p.hasRotateKey && !p.keyOrder.empty()) {
        // rotate was added to a loaded part: Wokwi writes it before attrs.
        Json reordered = Json::object();
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() == "rotate")
                continue;
            if (it.key() == "attrs")
                reordered["rotate"] = j["rotate"];
            reordered[it.key()] = it.value();
        }
        if (!reordered.contains("rotate"))
            reordered["rotate"] = j["rotate"];
        j = std::move(reordered);
    }
    for (auto it = p.extra.begin(); it != p.extra.end(); ++it)
        if (!j.contains(it.key()))
            j[it.key()] = it.value();
    return j;
}

Wire wireFromJson(const Json& j, std::vector<std::string>* warnings)
{
    if (!j.is_array() || j.size() < 2 || !j[0].is_string() || !j[1].is_string())
        throw LoadError("connection is not [from, to, ...]");
    Wire w;
    auto ref = [&](const Json& s) {
        auto r = PinRef::parse(s.get<std::string>());
        if (!r) {
            if (warnings)
                warnings->push_back("bad pin reference \"" + s.get<std::string>() + "\"");
            return PinRef{s.get<std::string>(), ""};
        }
        return *r;
    };
    w.from = ref(j[0]);
    w.to = ref(j[1]);
    w.color = (j.size() > 2 && j[2].is_string()) ? j[2].get<std::string>() : std::string{};
    w.hasPathElement = j.size() > 3;
    if (w.hasPathElement) {
        const Json& raw = j[3];
        std::optional<WirePath> parsed;
        if (raw.is_array()) {
            std::vector<std::string> items;
            bool allStrings = true;
            for (const Json& e : raw) {
                if (!e.is_string()) {
                    allStrings = false;
                    break;
                }
                items.push_back(e.get<std::string>());
            }
            if (allStrings)
                parsed = parseWirePath(items);
        }
        if (parsed)
            w.path = *parsed;
        else {
            w.rawPath = raw;
            if (warnings)
                warnings->push_back("unparsed wire path " + raw.dump() + " on " + w.from.str());
        }
    }
    for (std::size_t i = 4; i < j.size(); ++i)
        w.extraElements.push_back(j[i]);
    return w;
}

Json wireToJson(const Wire& w)
{
    Json j = Json::array({w.from.str(), w.to.str(), w.color});
    if (w.hasPathElement || !w.path.source.empty() || w.path.hasStar || !w.extraElements.empty()) {
        if (w.rawPath)
            j.push_back(*w.rawPath);
        else
            j.push_back(Json(formatWirePath(w.path)));
    }
    for (const Json& e : w.extraElements)
        j.push_back(e);
    return j;
}

LoadResult loadWokwi(const std::string& text)
{
    Json root;
    try {
        root = Json::parse(text);
    } catch (const Json::parse_error& e) {
        throw LoadError(std::string("not valid JSON: ") + e.what());
    }
    if (!root.is_object())
        throw LoadError("top level is not a JSON object");

    LoadResult r;
    Document& d = r.doc;
    if (auto it = root.find("parts"); it != root.end()) {
        if (!it->is_array())
            throw LoadError("\"parts\" is not an array");
        for (const Json& pj : *it)
            d.parts.push_back(partFromJson(pj));
        *it = nullptr;
    }
    if (auto it = root.find("connections"); it != root.end()) {
        if (!it->is_array())
            throw LoadError("\"connections\" is not an array");
        for (const Json& cj : *it)
            d.wires.push_back(wireFromJson(cj, &r.warnings));
        *it = nullptr;
    }
    d.root = std::move(root);
    d.trailingNewline = !text.empty() && text.back() == '\n';
    return r;
}

LoadResult loadWokwiFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw LoadError("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return loadWokwi(ss.str());
}

Json toJson(const Document& doc)
{
    Json parts = Json::array();
    for (const Part& p : doc.parts)
        parts.push_back(partToJson(p));
    Json conns = Json::array();
    for (const Wire& w : doc.wires)
        conns.push_back(wireToJson(w));

    Json out = doc.root.is_object() ? doc.root : Json::object();
    out["parts"] = std::move(parts);       // replaces placeholder in place,
    out["connections"] = std::move(conns); // or appends if it was absent
    return out;
}

std::string saveWokwi(const Document& doc)
{
    std::string s = prettyPrint(toJson(doc));
    if (doc.trailingNewline)
        s += '\n';
    return s;
}

void saveWokwiFile(const Document& doc, const std::string& path)
{
    // Respect read-only files: the atomic rename below would otherwise
    // replace them silently.
    std::error_code ec;
    const auto st = std::filesystem::status(path, ec);
    if (!ec && std::filesystem::exists(st)
        && (st.permissions() & std::filesystem::perms::owner_write) == std::filesystem::perms::none)
        throw std::runtime_error(path + " is read-only; use Save As to save a copy");
    const std::string text = saveWokwi(doc);
    const std::string tmp = path + ".chiply-tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("cannot write " + tmp);
        out << text;
        if (!out.flush())
            throw std::runtime_error("write failed for " + tmp);
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
        throw std::runtime_error("cannot replace " + path);
}

} // namespace chiply
