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
// With a paste name format (see NameFormat), incoming ids that match it are
// renamed by stepping the number at '#' instead (all by the same step, the
// smallest that makes every one of them free); the rest follow the rules
// above.
struct NameFormat {
    // "r#_*": '#' is the number to step, '*' is any text, the rest literal.
    // Empty: no format. valid() is false (and error() says why) for a format
    // without exactly one '#'.
    explicit NameFormat(const std::string& format = {});
    bool active() const { return m_active; }
    bool valid() const { return m_error.empty(); }
    const std::string& error() const { return m_error; }
    // The id with its '#' number stepped by `step`, or nullopt if it does
    // not match. Leading zeros keep the width ("r07" + 1 -> "r08").
    std::optional<std::string> step(const std::string& id, long step) const;

private:
    bool m_active = false;
    std::string m_error;
    std::string m_regex;
};

std::vector<std::pair<std::string, std::string>> remapIds(const std::vector<Part>& incoming,
                                                          const std::set<std::string>& used,
                                                          const NameFormat& format = NameFormat());

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
                                        int* droppedWires = nullptr, const NameFormat& format = NameFormat());

// Find and replace in part ids: every occurrence of `from` in each of `ids`
// becomes `to`. Returns old -> new for the ids that change. *error is set
// (and nothing returned) if a new id is empty, repeated, or taken by a part
// outside `ids`.
std::map<std::string, std::string> replaceInIds(const Document& doc, const std::vector<std::string>& ids,
                                                const std::string& from, const std::string& to, std::string* error);
// Renames parts all at once (swaps work) and their wire ends.
void renameParts(Document& doc, const std::map<std::string, std::string>& rename);

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
