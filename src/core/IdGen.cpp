#include "core/IdGen.h"

#include <array>
#include <cctype>
#include <string_view>

namespace chiply {

namespace {

struct TypePrefix {
    std::string_view type;
    std::string_view prefix;
};

constexpr std::array kTypePrefixes{
    TypePrefix{"wokwi-gate-and-2", "and"},       TypePrefix{"wokwi-gate-or-2", "or"},
    TypePrefix{"wokwi-gate-xor-2", "xor"},       TypePrefix{"wokwi-gate-nand-2", "nand"},
    TypePrefix{"wokwi-gate-nor-2", "nor"},       TypePrefix{"wokwi-gate-xnor-2", "xnor"},
    TypePrefix{"wokwi-gate-not", "not"},         TypePrefix{"wokwi-gate-buffer", "buf"},
    TypePrefix{"wokwi-mux-2", "mux"},            TypePrefix{"wokwi-flip-flop-d", "flop"},
    TypePrefix{"wokwi-flip-flop-dr", "flop"},    TypePrefix{"wokwi-flip-flop-dsr", "flop"},
    TypePrefix{"wokwi-text", "text"},            TypePrefix{"wokwi-gnd", "gnd"},
    TypePrefix{"wokwi-vcc", "vcc"},              TypePrefix{"wokwi-clock-generator", "clock"},
    TypePrefix{"wokwi-pushbutton", "btn"},       TypePrefix{"wokwi-slide-switch", "sw"},
    TypePrefix{"wokwi-dip-switch-8", "sw"},      TypePrefix{"wokwi-7segment", "sevseg"},
    TypePrefix{"wokwi-led", "led"},              TypePrefix{"wokwi-resistor", "r"},
    TypePrefix{"wokwi-logic-analyzer", "logic"}, TypePrefix{"wokwi-pi-pico", "pico"},
};

// Prefixes treated as auto-generated even if no type maps to them
// ("pwr" is what Wokwi names VCC and GND symbols in many projects).
constexpr std::array<std::string_view, 2> kExtraAutoPrefixes{"pwr", "chip"};

constexpr std::string_view kVerilogKeywords[] = {
    "always", "and", "assign", "automatic", "begin", "buf", "bufif0", "bufif1", "case", "casex",
    "casez", "cell", "cmos", "config", "deassign", "default", "defparam", "design", "disable",
    "edge", "else", "end", "endcase", "endconfig", "endfunction", "endgenerate", "endmodule",
    "endprimitive", "endspecify", "endtable", "endtask", "event", "for", "force", "forever",
    "fork", "function", "generate", "genvar", "highz0", "highz1", "if", "ifnone", "incdir",
    "include", "initial", "inout", "input", "instance", "integer", "join", "large", "liblist",
    "library", "localparam", "macromodule", "medium", "module", "nand", "negedge", "nmos", "nor",
    "noshowcancelled", "not", "notif0", "notif1", "or", "output", "parameter", "pmos", "posedge",
    "primitive", "pull0", "pull1", "pulldown", "pullup", "pulsestyle_ondetect",
    "pulsestyle_onevent", "rcmos", "real", "realtime", "reg", "release", "repeat", "rnmos",
    "rpmos", "rtran", "rtranif0", "rtranif1", "scalared", "showcancelled", "signed", "small",
    "specify", "specparam", "strong0", "strong1", "supply0", "supply1", "table", "task", "time",
    "tran", "tranif0", "tranif1", "tri", "tri0", "tri1", "triand", "trior", "trireg", "unsigned",
    "use", "uwire", "vectored", "wait", "wand", "weak0", "weak1", "while", "wire", "wor", "xnor",
    "xor", "logic", "bit", "byte"};

} // namespace

std::pair<std::string, long> splitTrailingNumber(const std::string& id)
{
    std::size_t i = id.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(id[i - 1])))
        --i;
    if (i == id.size() || id.size() - i > 9)
        return {id, -1};
    return {id.substr(0, i), std::stol(id.substr(i))};
}

std::string idPrefixForType(const std::string& type)
{
    for (const auto& tp : kTypePrefixes)
        if (tp.type == type)
            return std::string(tp.prefix);
    // Fallback: last dash-separated word without digits ("board-tt-block-input" -> "input").
    std::string base = type;
    auto dash = base.rfind('-');
    if (dash != std::string::npos)
        base = base.substr(dash + 1);
    std::string out;
    for (char c : base)
        if (std::isalpha(static_cast<unsigned char>(c)))
            out += c;
    return out.empty() ? std::string("part") : out;
}

bool isAutoId(const std::string& id)
{
    auto [prefix, n] = splitTrailingNumber(id);
    if (n < 0)
        return false;
    for (const auto& tp : kTypePrefixes)
        if (tp.prefix == prefix)
            return true;
    for (auto p : kExtraAutoPrefixes)
        if (p == prefix)
            return true;
    return false;
}

std::string nextFreeId(const std::string& prefix, const std::set<std::string>& used)
{
    long highest = 0;
    for (const std::string& id : used) {
        auto [p, n] = splitTrailingNumber(id);
        if (p == prefix && n > highest)
            highest = n;
    }
    std::string candidate;
    do {
        candidate = prefix + std::to_string(++highest);
    } while (used.count(candidate));
    return candidate;
}

bool isVerilogKeyword(const std::string& s)
{
    for (auto k : kVerilogKeywords)
        if (k == s)
            return true;
    return false;
}

bool isValidInstanceName(const std::string& s)
{
    if (s.empty())
        return false;
    auto first = static_cast<unsigned char>(s[0]);
    if (!(std::isalpha(first) || s[0] == '_'))
        return false;
    for (char c : s) {
        auto u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '_' || c == '$'))
            return false;
    }
    return !isVerilogKeyword(s);
}

} // namespace chiply
