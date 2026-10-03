#pragma once
// Structural edits on a Document, shared by the editor's undo commands, the
// clipboard and the CLI. Each edit returns what is needed to undo it.
#include "core/Document.h"
#include "core/WirePath.h"

#include <optional>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace chiply {

std::set<std::string> usedIds(const Document& doc);

// Renumbers incoming part ids so they fit into a document whose ids are
// `used` (PLAN.md 4.7):
//  - auto ids (<known prefix><n>) always get the next free number for their
//    prefix, in fragment order, so a pasted row stays in sequence;
//  - user names are kept if free, otherwise get the lowest free "_N" suffix;
//  - duplicates inside the fragment are resolved the same way.
// Returns old id -> new id for every incoming part (in order).
std::vector<std::pair<std::string, std::string>> remapIds(const std::vector<Part>& incoming,
                                                          const std::set<std::string>& used);

// A set of parts plus the wires whose both ends are among them.
struct Fragment {
    std::vector<Part> parts;
    std::vector<Wire> wires;
};
Fragment extractFragment(const Document& doc, const std::set<std::string>& partIds);

// Clipboard text: a Wokwi-format JSON object {"parts": [...],
// "connections": [...]}, formatted like diagram.json.
std::string fragmentToText(const Fragment& f);
// Accepts a fragment or a whole diagram.json; nullopt if the text is not
// one (or has no parts).
std::optional<Fragment> fragmentFromText(const std::string& text);

// Top-left of the parts' unrotated positions (min left, min top).
Point fragmentOrigin(const Fragment& f);

// Adds a fragment, renumbering ids and offsetting positions by (dx, dy).
// Returns the new part ids (fragment order). Wires that reference a part not
// in the fragment are dropped and counted in *droppedWires.
std::vector<std::string> insertFragment(Document& doc, Fragment frag, double dx, double dy,
                                        int* droppedWires = nullptr);

// Removal with enough information to put everything back exactly.
struct Removed {
    std::vector<std::pair<std::size_t, Part>> parts; // original index, part
    std::vector<std::pair<std::size_t, Wire>> wires; // original index, wire
};
// Removes the given parts, every wire attached to them, and the given wires.
Removed removeItems(Document& doc, const std::set<std::string>& partIds, const std::set<std::size_t>& wireIndices);
void restoreItems(Document& doc, const Removed& r);

// Removes the parts/wires that insertFragment added (the last ones).
void removeLast(Document& doc, std::size_t parts, std::size_t wires);

} // namespace chiply
