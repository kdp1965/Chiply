#include "core/Document.h"

namespace chiply {

std::optional<PinRef> PinRef::parse(const std::string& ref)
{
    auto colon = ref.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == ref.size())
        return std::nullopt;
    return PinRef{ref.substr(0, colon), ref.substr(colon + 1)};
}

std::string Document::author() const
{
    auto it = root.find("author");
    return (it != root.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

void Document::setAuthor(const std::string& a)
{
    root["author"] = a;
}

Part* Document::findPart(const std::string& id)
{
    for (Part& p : parts)
        if (p.id == id)
            return &p;
    return nullptr;
}

const Part* Document::findPart(const std::string& id) const
{
    return const_cast<Document*>(this)->findPart(id);
}

bool Document::renamePart(const std::string& from, const std::string& to)
{
    Part* p = findPart(from);
    if (!p || findPart(to))
        return false;
    p->id = to;
    for (Wire& w : wires) {
        if (w.from.part == from)
            w.from.part = to;
        if (w.to.part == from)
            w.to.part = to;
    }
    return true;
}

Document Document::makeEmpty(const std::string& author)
{
    Document d;
    d.root = Json::object();
    d.root["version"] = 1;
    d.root["author"] = author;
    d.root["editor"] = "wokwi";
    d.root["parts"] = nullptr;
    d.root["connections"] = nullptr;
    d.root["dependencies"] = Json::object();
    return d;
}

} // namespace chiply
