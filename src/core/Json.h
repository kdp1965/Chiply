#pragma once
// Chiply uses nlohmann::ordered_json everywhere so object key order from
// Wokwi files is preserved (QJsonObject would sort keys alphabetically).
#include <nlohmann/json.hpp>

namespace chiply {
using Json = nlohmann::ordered_json;
}
