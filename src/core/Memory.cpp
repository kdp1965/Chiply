#include "core/Memory.h"

#include "core/Blocks.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace chiply {

std::optional<MemoryInfo> parseMemoryType(const std::string& type)
{
    static const std::regex re("chiply-(ram|rom)-([0-9]+)x([0-9]+)");
    std::smatch m;
    if (!std::regex_match(type, m, re))
        return std::nullopt;
    MemoryInfo info;
    info.rom = m[1] == "rom";
    try {
        info.depth = std::stoi(m[2]);
        info.width = std::stoi(m[3]);
    } catch (...) {
        return std::nullopt;
    }
    if (info.depth < 2 || info.depth > 256 || (info.depth & (info.depth - 1)) || info.width < 1 || info.width > 16)
        return std::nullopt;
    info.abits = 0;
    while ((1 << info.abits) < info.depth)
        ++info.abits;
    return info;
}

std::string memoryType(bool rom, int depth, int width)
{
    return std::string(rom ? "chiply-rom-" : "chiply-ram-") + std::to_string(depth) + "x" + std::to_string(width);
}

bool isMemoryType(const std::string& type) { return parseMemoryType(type).has_value(); }

PartDef memoryPartDef(const MemoryInfo& m)
{
    std::vector<AutoPin> left, right;
    if (!m.rom) {
        left.push_back({"clk", PinDir::In, true});
        left.push_back({"we", PinDir::In, false});
    }
    for (int i = 0; i < m.abits; ++i)
        left.push_back({"a" + std::to_string(i), PinDir::In, false});
    if (!m.rom)
        for (int i = 0; i < m.width; ++i)
            left.push_back({"d" + std::to_string(i), PinDir::In, false});
    for (int i = 0; i < m.width; ++i)
        right.push_back({"q" + std::to_string(i), PinDir::Out, false});
    const std::string size = std::to_string(m.depth) + "x" + std::to_string(m.width);
    PartDef d = autoSymbolPart(memoryType(m.rom, m.depth, m.width), (m.rom ? "ROM " : "RAM ") + size, left, right);
    d.category = "Chiply cells";
    d.prefix = m.rom ? "rom" : "ram";
    d.verilog = {{"memory", m.rom ? "rom" : "ram"}};
    if (m.rom)
        d.attrs = {{"data", ""}, {"file", ""}};
    d.source = "Chiply memory (PLAN.md 7.3)";
    d.memory = std::make_shared<const MemoryInfo>(m);
    return d;
}

std::vector<std::uint32_t> parseMemoryText(const std::string& text, const MemoryInfo& m, std::string* error)
{
    std::vector<std::uint32_t> words(std::size_t(m.depth), 0);
    std::size_t i = 0, addr = 0;
    const std::uint32_t maxWord = m.width >= 32 ? 0xffffffffu : ((1u << m.width) - 1);
    auto fail = [&](const std::string& why) {
        if (error && error->empty())
            *error = why;
    };
    while (i < text.size()) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            ++i;
        } else if (text.compare(i, 2, "//") == 0) {
            while (i < text.size() && text[i] != '\n')
                ++i;
        } else if (text.compare(i, 2, "/*") == 0) {
            const std::size_t e = text.find("*/", i + 2);
            i = e == std::string::npos ? text.size() : e + 2;
        } else {
            const bool isAddr = c == '@';
            if (isAddr)
                ++i;
            std::string tok;
            while (i < text.size() && (std::isxdigit(static_cast<unsigned char>(text[i])) || text[i] == '_')) {
                if (text[i] != '_')
                    tok += text[i];
                ++i;
            }
            if (tok.empty()) {
                fail("unexpected \"" + std::string(1, text[i]) + "\" in the contents");
                return words;
            }
            const unsigned long v = std::stoul(tok.substr(0, 9), nullptr, 16);
            if (isAddr) {
                addr = v;
                continue;
            }
            if (addr >= words.size()) {
                fail("more than " + std::to_string(m.depth) + " words");
                return words;
            }
            if (tok.size() > 8 || v > maxWord) {
                fail("word " + tok + " is wider than " + std::to_string(m.width) + " bits");
                return words;
            }
            words[addr++] = std::uint32_t(v);
        }
    }
    return words;
}

std::vector<std::uint32_t> memoryContents(const Part& part, const MemoryInfo& m, const std::string& baseDir,
                                          std::string* error)
{
    auto attr = [&](const char* key) {
        return part.attrs.is_object() && part.attrs.contains(key) && part.attrs[key].is_string()
            ? part.attrs[key].get<std::string>()
            : std::string();
    };
    const std::string file = attr("file");
    std::string text = attr("data");
    if (!file.empty()) {
        namespace fs = std::filesystem;
        fs::path p(file);
        if (p.is_relative() && !baseDir.empty())
            p = fs::path(baseDir) / p;
        std::ifstream in(p, std::ios::binary);
        if (!in) {
            if (error)
                *error = "cannot read " + p.string();
            return std::vector<std::uint32_t>(std::size_t(m.depth), 0);
        }
        std::stringstream ss;
        ss << in.rdbuf();
        text = ss.str();
    }
    return parseMemoryText(text, m, error);
}

} // namespace chiply
