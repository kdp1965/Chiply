#pragma once
// Reading and writing Wokwi diagram.json files.
//
// Fidelity contract (PLAN.md 3.6): loading a Wokwi file and saving it with no
// edits reproduces the file byte for byte, given Wokwi's own formatting.
#include "core/Document.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace chiply {

struct LoadError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct LoadResult {
    Document doc;
    std::vector<std::string> warnings; // recoverable oddities, e.g. bad pin refs
};

LoadResult loadWokwi(const std::string& text);          // throws LoadError
LoadResult loadWokwiFile(const std::string& path);      // throws LoadError

Json toJson(const Document& doc);
std::string saveWokwi(const Document& doc);
void saveWokwiFile(const Document& doc, const std::string& path); // atomic replace

// Part <-> JSON object, shared with the clipboard code later.
Part partFromJson(const Json& j);
Json partToJson(const Part& p);
Wire wireFromJson(const Json& j, std::vector<std::string>* warnings = nullptr);
Json wireToJson(const Wire& w);

} // namespace chiply
