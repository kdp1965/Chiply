#include "core/JsonFormat.h"

#include <cmath>

namespace chiply {

std::string formatNumber(double v)
{
    if (std::isfinite(v) && v == std::trunc(v) && std::fabs(v) < 1e15) {
        if (v == 0.0)
            return "0"; // also turns -0 into 0, as JavaScript does
        return std::to_string(static_cast<long long>(v));
    }
    return Json(v).dump(); // shortest round-trip representation
}

double round2(double v)
{
    double r = std::round(v * 100.0) / 100.0;
    return r == 0.0 ? 0.0 : r;
}

namespace {

std::string scalar(const Json& v)
{
    if (v.is_number_float())
        return formatNumber(v.get<double>());
    return v.dump(-1, ' ', false, Json::error_handler_t::replace);
}

// Single-line rendering, Prettier style.
std::string flat(const Json& v)
{
    if (v.is_object()) {
        if (v.empty())
            return "{}";
        std::string s = "{ ";
        bool first = true;
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (!first)
                s += ", ";
            first = false;
            s += Json(it.key()).dump() + ": " + flat(it.value());
        }
        return s + " }";
    }
    if (v.is_array()) {
        if (v.empty())
            return "[]";
        std::string s = "[ ";
        bool first = true;
        for (const auto& e : v) {
            if (!first)
                s += ", ";
            first = false;
            s += flat(e);
        }
        return s + " ]";
    }
    return scalar(v);
}

// Appends `v` starting at column `col`. `tail` is the number of characters
// that must follow on the same line (1 for a separating comma, else 0).
void emit(const Json& v, int indent, int col, int tail, int width, std::string& out)
{
    std::string oneLine = flat(v);
    bool container = v.is_object() || v.is_array();
    if (!container || v.empty() || col + static_cast<int>(oneLine.size()) + tail <= width) {
        out += oneLine;
        return;
    }
    const std::string pad(indent + 2, ' ');
    const bool isObj = v.is_object();
    out += isObj ? "{\n" : "[\n";
    std::size_t i = 0, n = v.size();
    for (auto it = v.begin(); it != v.end(); ++it, ++i) {
        bool last = (i + 1 == n);
        out += pad;
        int c = indent + 2;
        if (isObj) {
            std::string key = Json(it.key()).dump() + ": ";
            out += key;
            c += static_cast<int>(key.size());
        }
        emit(it.value(), indent + 2, c, last ? 0 : 1, width, out);
        out += last ? "\n" : ",\n";
    }
    out += std::string(indent, ' ');
    out += isObj ? "}" : "]";
}

} // namespace

std::string prettyPrint(const Json& value, int width)
{
    std::string out;
    emit(value, 0, 0, 0, width, out);
    return out;
}

} // namespace chiply
