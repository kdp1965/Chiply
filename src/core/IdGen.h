#pragma once
// Part ids are instance names: Wokwi's Verilog export uses them verbatim
// (`dffsr_cell state_reg_2 (...)`). New ids follow Wokwi's `<prefix><n>`
// convention with the next number above the highest one in use.
#include <set>
#include <string>
#include <utility>

namespace chiply {

// "and326" -> {"and", 326}; "state_reg" -> {"state_reg", -1}.
std::pair<std::string, long> splitTrailingNumber(const std::string& id);

// Prefix Wokwi uses for a part type ("wokwi-gate-and-2" -> "and",
// "wokwi-flip-flop-dsr" -> "flop").
std::string idPrefixForType(const std::string& type);

// True for ids of the form <known prefix><number>, i.e. ones the editor
// generated rather than names the user chose.
bool isAutoId(const std::string& id);

// `prefix` + (highest number already used with that prefix + 1).
std::string nextFreeId(const std::string& prefix, const std::set<std::string>& used);

bool isVerilogKeyword(const std::string& s);
// Legal, non-keyword Verilog simple identifier.
bool isValidInstanceName(const std::string& s);

} // namespace chiply
