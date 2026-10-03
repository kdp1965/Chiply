#include "core/Verilog.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <map>
#include <sstream>

namespace chiply {

extern const char* const kTtCellsV;
const char* ttCellsV() { return kTtCellsV; }

namespace {

bool isTtBlock(const std::string& type) { return type.rfind("board-tt-block", 0) == 0; }

std::string attrString(const Part& p, const char* key)
{
    if (!p.attrs.is_object() || !p.attrs.contains(key))
        return {};
    const Json& v = p.attrs[key];
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_number_integer())
        return std::to_string(v.get<long long>());
    return {};
}

} // namespace

std::string defaultModuleName(const std::string& stem)
{
    std::string s = "tt_um_";
    for (char c : stem)
        s += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    return s;
}

std::string writeVerilog(const Document& doc, const PartLibrary& lib, const VerilogOptions& opt)
{
    const Netlist nl = Netlist::build(doc, lib);

    // What cannot be exported.
    std::vector<std::string> problems;
    for (const Part& p : doc.parts) {
        const PartDef* def = lib.find(p.type);
        if (!def)
            problems.push_back(p.id + ": unknown part type " + p.type);
        else if (p.type == "wokwi-flip-flop-sr")
            problems.push_back(p.id + ": SR flip-flops have no cell in cells.v");
    }
    if (!problems.empty()) {
        std::string m = "cannot export:";
        for (const std::string& s : problems)
            m += "\n  " + s;
        throw ExportError(m);
    }

    // Net numbers and what each net is tied to.
    std::vector<int> number(nl.nets.size(), 0);
    std::vector<std::string> init(nl.nets.size()); // "clk", "ui_in[0]", "1'b0", ...
    std::vector<int> order;
    auto connected = [&](int net) { return net >= 0 && nl.nets[size_t(net)].pins.size() >= 2; };
    auto num = [&](int net) {
        if (net >= 0 && number[size_t(net)] == 0) {
            number[size_t(net)] = int(order.size()) + 1;
            order.push_back(net);
        }
    };
    auto pinNet = [&](const Part& p, const std::string& pin) { return nl.netOf(PinRef{p.id, pin}); };

    // 1. Tiny Tapeout blocks, in part order.
    std::string uoOut[8], uioOut[8], uioOe[8]; // assigned net names
    for (const Part& p : doc.parts) {
        if (!isTtBlock(p.type))
            continue;
        auto port = [&](const std::string& pin, const std::string& name, bool input) {
            const int net = pinNet(p, pin);
            if (!connected(net))
                return;
            num(net);
            if (input && init[size_t(net)].empty())
                init[size_t(net)] = name;
        };
        if (p.type == "board-tt-block-input" || p.type == "board-tt-block-input-8") {
            if (p.type == "board-tt-block-input") {
                port("CLK", "clk", true);
                port("RST_N", "rst_n", true);
            }
            for (int b = 0; b < 8; ++b)
                port("IN" + std::to_string(b), "ui_in[" + std::to_string(b) + "]", true);
        } else if (p.type == "board-tt-block-output") {
            for (int b = 0; b < 8; ++b)
                port("OUT" + std::to_string(b), {}, false);
        } else if (p.type == "board-tt-block-bidirectional-io") {
            const std::string bit = attrString(p, "verilogBit");
            port("IN", bit.empty() ? std::string() : "uio_in[" + bit + "]", true);
            port("OUT", {}, false);
            port("OE", {}, false);
        }
    }
    // 2. Every part in order: constants and cell pins.
    for (const Part& p : doc.parts) {
        const PartDef* def = lib.find(p.type);
        if (!def || !def->verilog.is_object())
            continue;
        if (def->verilog.contains("constant")) {
            const int net = def->pins.empty() ? -1 : pinNet(p, def->pins.front().name);
            num(net);
            if (net >= 0 && init[size_t(net)].empty())
                init[size_t(net)] = def->verilog["constant"].get<std::string>();
        } else if (def->verilog.contains("cell")) {
            for (const PinDef& pin : def->pins) {
                const int net = pinNet(p, pin.name);
                if (connected(net))
                    num(net);
            }
        }
    }
    auto name = [&](int net) -> std::string {
        return (net >= 0 && number[size_t(net)]) ? "net" + std::to_string(number[size_t(net)]) : std::string();
    };

    // Output assignments (by bit; a later block wins a repeated bit, as Wokwi).
    for (const Part& p : doc.parts) {
        if (p.type == "board-tt-block-output") {
            for (int b = 0; b < 8; ++b)
                uoOut[b] = name(pinNet(p, "OUT" + std::to_string(b)));
        } else if (p.type == "board-tt-block-bidirectional-io") {
            const std::string bit = attrString(p, "verilogBit");
            if (bit.size() == 1 && bit[0] >= '0' && bit[0] <= '7') {
                const int b = bit[0] - '0';
                uioOut[b] = name(pinNet(p, "OUT"));
                uioOe[b] = name(pinNet(p, "OE"));
            }
        }
    }

    std::ostringstream o;
    o << (opt.headerComment.empty()
              ? "/* Automatically generated by Chiply" + (opt.sourceName.empty() ? std::string() : " from " + opt.sourceName) + " */"
              : opt.headerComment)
      << "\n\n`default_nettype none\n\n// verilator lint_off UNUSEDSIGNAL\n// verilator lint_off PINCONNECTEMPTY\n\n"
      << "module " << opt.moduleName << "(\n"
      << "  input  wire [7:0] ui_in,    // Dedicated inputs\n"
      << "  output wire [7:0] uo_out,    // Dedicated outputs\n"
      << "  input  wire [7:0] uio_in,    // IOs: Input path\n"
      << "  output wire [7:0] uio_out,    // IOs: Output path\n"
      << "  output wire [7:0] uio_oe,    // IOs: Enable path (active high: 0=input, 1=output)\n"
      << "  input ena,\n  input clk,\n  input rst_n\n);\n";
    for (std::size_t i = 0; i < order.size(); ++i) {
        const std::string& in = init[size_t(order[i])];
        o << "  wire net" << i + 1 << (in.empty() ? std::string() : " = " + in) << ";\n";
    }
    o << "\n";
    auto rhs = [](const std::string& n) { return n.empty() ? std::string("1'b0") : n; };
    for (int b = 0; b < 8; ++b)
        o << "  assign uo_out[" << b << "] = " << rhs(uoOut[b]) << ";\n";
    for (int b = 0; b < 8; ++b)
        o << "  assign uio_out[" << b << "] = " << rhs(uioOut[b]) << ";\n"
          << "  assign uio_oe[" << b << "] = " << rhs(uioOe[b]) << ";\n";
    o << "\n";
    for (const Part& p : doc.parts) {
        const PartDef* def = lib.find(p.type);
        if (!def || !def->verilog.is_object() || !def->verilog.contains("cell"))
            continue;
        const Json& ports = def->verilog["ports"];
        o << "  " << def->verilog["cell"].get<std::string>() << " " << p.id << " (\n";
        bool first = true;
        for (const PinDef& pin : def->pins) {
            if (!ports.contains(pin.name))
                continue;
            o << (first ? "" : ",\n") << "    ." << ports[pin.name].get<std::string>() << " (" << name(pinNet(p, pin.name)) << ")";
            first = false;
        }
        o << "\n  );\n";
    }
    o << "endmodule\n";
    return o.str();
}

namespace {

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);)
        lines.push_back(l);
    return lines;
}

// "  key:  value   # comment" -> replaces value, keeps indentation, the
// spacing before the value and the comment.
std::string setYamlValue(const std::string& line, const std::string& value)
{
    static const std::regex re(R"(^(\s*[A-Za-z_]+:\s*)("[^"]*"|'[^']*'|[^#\s]*)(\s*(#.*)?)$)");
    std::smatch m;
    if (!std::regex_match(line, m, re))
        return line;
    return m[1].str() + "\"" + value + "\"" + m[3].str();
}

} // namespace

std::string infoYamlTopModule(const std::string& yaml)
{
    static const std::regex re(R"(^\s*top_module:\s*["']?([A-Za-z0-9_]+)["']?)");
    for (const std::string& l : splitLines(yaml)) {
        std::smatch m;
        if (std::regex_search(l, m, re))
            return m[1].str();
    }
    return {};
}

std::string patchInfoYaml(const std::string& yaml, const std::string& topModule, const std::vector<std::string>& sources)
{
    std::vector<std::string> in = splitLines(yaml), out;
    bool hasTop = false, hasSources = false;
    for (const std::string& l : in) {
        const std::string t = l.substr(std::min(l.find_first_not_of(' '), l.size()));
        hasTop |= t.rfind("top_module:", 0) == 0;
        hasSources |= t.rfind("source_files:", 0) == 0;
    }
    for (std::size_t i = 0; i < in.size(); ++i) {
        const std::string& l = in[i];
        const std::string t = l.substr(std::min(l.find_first_not_of(' '), l.size()));
        if (t.rfind("language:", 0) == 0) {
            out.push_back(setYamlValue(l, "Verilog"));
            // A Wokwi project's info.yaml has neither of these: add them here.
            const std::string indent = l.substr(0, l.size() - t.size());
            if (!hasTop)
                out.push_back(indent + "top_module:   \"" + topModule + "\"");
            if (!hasSources) {
                out.push_back(indent + "source_files:");
                for (const std::string& src : sources)
                    out.push_back(indent + "  - \"" + src + "\"");
            }
        } else if (t.rfind("top_module:", 0) == 0) {
            out.push_back(setYamlValue(l, topModule));
        } else if (t.rfind("wokwi_id:", 0) == 0) {
            out.push_back(l.substr(0, l.size() - t.size()) + "# " + t + "  # exported from Chiply as Verilog");
        } else if (t.rfind("source_files:", 0) == 0) {
            out.push_back(l);
            // Replace the list items that follow (keep their indentation).
            std::string indent = l.substr(0, l.size() - t.size()) + "  ";
            std::size_t j = i + 1;
            while (j < in.size()) {
                const std::string& n = in[j];
                const std::size_t k = n.find_first_not_of(' ');
                if (k != std::string::npos && n[k] == '-') {
                    indent = n.substr(0, k);
                    ++j;
                } else if (k != std::string::npos && n[k] == '#' && j + 1 < in.size()
                           && in[j + 1].find_first_not_of(' ') != std::string::npos
                           && in[j + 1][in[j + 1].find_first_not_of(' ')] == '-') {
                    ++j; // a comment between list items
                } else {
                    break;
                }
            }
            for (const std::string& src : sources)
                out.push_back(indent + "- \"" + src + "\"");
            i = j - 1;
        } else {
            out.push_back(l);
        }
    }
    std::string r;
    for (const std::string& l : out)
        r += l + "\n";
    return r;
}

std::vector<std::string> exportTtProject(const Document& doc, const PartLibrary& lib, const std::string& dir,
                                         const VerilogOptions& opt)
{
    namespace fs = std::filesystem;
    const std::string verilog = writeVerilog(doc, lib, opt); // throws before anything is written
    std::vector<std::string> done;
    const fs::path root(dir);
    fs::create_directories(root / "src");
    auto write = [&](const fs::path& p, const std::string& text) {
        std::ofstream f(p, std::ios::binary);
        if (!f)
            throw std::runtime_error("cannot write " + p.string());
        f << text;
        if (!f)
            throw std::runtime_error("cannot write " + p.string());
        done.push_back("wrote " + p.string());
    };
    const std::string vName = opt.moduleName + ".v";
    write(root / "src" / vName, verilog);
    write(root / "src" / "cells.v", ttCellsV());
    auto readText = [](const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    const fs::path info = root / "info.yaml";
    if (fs::exists(info)) {
        const std::string before = readText(info);
        const std::string after = patchInfoYaml(before, opt.moduleName, {vName, "cells.v"});
        if (after != before)
            write(info, after);
    } else {
        done.push_back("no info.yaml in " + root.string() + ": set language, top_module and source_files by hand");
    }
    const fs::path mk = root / "test" / "Makefile";
    if (fs::exists(mk)) {
        std::string text = readText(mk);
        static const std::regex re(R"((^|\n)(PROJECT_SOURCES\s*=)[^\n]*)");
        const std::string patched = std::regex_replace(text, re, "$1$2 " + vName + " cells.v");
        if (patched != text)
            write(mk, patched);
    }
    return done;
}

} // namespace chiply
