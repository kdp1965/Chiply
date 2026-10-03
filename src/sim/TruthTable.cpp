#include "sim/TruthTable.h"

#include <cctype>
#include <regex>
#include <sstream>

namespace chiply::sim {

namespace {

std::string trim(const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

// The first 8 value characters of a cell (after an optional 8' or 0b
// prefix), lower case, spaces removed; "" if the cell does not start with 8.
std::string cellBits(const std::string& cell, const std::string& allowed)
{
    std::size_t i = 0;
    auto skipSpace = [&] {
        while (i < cell.size() && std::isspace(static_cast<unsigned char>(cell[i])))
            ++i;
    };
    skipSpace();
    if (i + 1 < cell.size() && std::isdigit(static_cast<unsigned char>(cell[i])) && cell[i + 1] == '\'')
        i += 2;
    else if (cell.compare(i, 2, "0b") == 0 && i + 2 < cell.size() && allowed.find(cell[i + 2]) != std::string::npos)
        i += 2; // only a prefix when value characters follow
    std::string bits;
    while (bits.size() < 8) {
        skipSpace();
        if (i >= cell.size())
            return {};
        const char c = char(std::tolower(static_cast<unsigned char>(cell[i])));
        if (allowed.find(c) == std::string::npos)
            return {};
        bits += c;
        ++i;
    }
    return bits;
}

class Builder {
public:
    TruthTable table;

    void step(const std::string& inBits, const std::string& outBits, int line, const std::string& comment)
    {
        if (inBits.find('c') != std::string::npos) {
            std::string setup = inBits;
            for (char& c : setup)
                if (c == 'c')
                    c = 'x';
            step(setup, "", line, comment);
            std::string clock = inBits;
            for (char& c : clock)
                c = (c == 't') ? 'x' : (c == 'c') ? 't' : c;
            step(clock, "", line, comment);
            step(clock, "", line, comment);
        }
        std::string state(8, '0');
        for (int i = 0; i < 8; ++i) {
            const char c = inBits[size_t(i)];
            if (c == '0' || c == '1')
                m_cur[size_t(i)] = c;
            else if (c == 't')
                m_cur[size_t(i)] = m_cur[size_t(i)] == '1' ? '0' : '1';
            state[size_t(i)] = m_cur[size_t(i)];
        }
        std::string out = outBits;
        for (char& c : out)
            if (c == 'x')
                c = '-';
        table.steps.push_back({state, out, line, comment});
    }

private:
    std::string m_cur = std::string(8, '0');
};

} // namespace

TruthTable parseTruthTable(const std::string& text)
{
    Builder b;
    std::vector<std::string> lines;
    {
        std::istringstream in(text);
        for (std::string l; std::getline(in, l);)
            lines.push_back(l);
    }
    // Yosys-style table: rows "8'bits | 8'bits", no state.
    static const std::regex simple(R"(^\s*\d'([01]+)\s*\|\s*\d'([zZxX01]+))");
    bool isSimple = false;
    for (std::size_t n = 0; n < lines.size(); ++n) {
        std::smatch m;
        if (std::regex_search(lines[n], m, simple) && m[1].length() == 8 && m[2].length() == 8) {
            isSimple = true;
            if (m[2].str() == "x" || m[2].str() == "xxxxxxxx")
                continue;
            std::string out = m[2].str();
            for (char& c : out)
                c = (c == '0' || c == '1') ? c : '-';
            b.table.steps.push_back({m[1].str(), out, int(n + 1), {}});
        }
    }
    if (isSimple)
        return b.table;

    // Markdown: skip through the header separator |---|---|.
    static const std::regex sep(R"(^\s*(\|\s*:?-+:?\s*){2,}\|\s*$)");
    std::size_t first = 0;
    for (std::size_t n = 0; n < lines.size(); ++n)
        if (std::regex_match(lines[n], sep)) {
            first = n + 1;
            break;
        }
    for (std::size_t n = first; n < lines.size(); ++n) {
        const std::string l = trim(lines[n]);
        if (l.size() < 3 || l.front() != '|' || l.back() != '|')
            continue;
        std::vector<std::string> cells;
        std::string cur;
        for (std::size_t i = 1; i < l.size(); ++i) {
            if (l[i] == '|') {
                cells.push_back(cur);
                cur.clear();
            } else {
                cur += l[i];
            }
        }
        if (cells.empty())
            continue;
        const std::string in = cellBits(cells[0], "01tcx-");
        if (in.empty()) {
            const std::string t = trim(cells[0]);
            if (!t.empty() && t.find('#') == std::string::npos && t.find("//") == std::string::npos
                && t.find_first_of("01x") != std::string::npos)
                b.table.warnings.push_back("line " + std::to_string(n + 1) + ": cannot read inputs \"" + t + "\"");
            continue;
        }
        std::string out;
        if (cells.size() > 1 && !trim(cells[1]).empty()) {
            out = cellBits(cells[1], "01x-");
            if (out.empty()) {
                b.table.warnings.push_back("line " + std::to_string(n + 1) + ": cannot read outputs \""
                                           + trim(cells[1]) + "\"");
                continue;
            }
        }
        b.step(in, out, int(n + 1), cells.size() > 2 ? trim(cells[2]) : std::string());
    }
    return b.table;
}

TruthResult runTruthTable(Simulator& sim, const TruthTable& table, const std::vector<int>& in,
                          const std::vector<int>& out, Time stepDelay)
{
    TruthResult r;
    auto ch = [](V v) {
        return v == V::L ? '0' : v == V::H ? '1' : v == V::X ? 'x' : 'z';
    };
    for (const TruthStep& s : table.steps) {
        for (int bit = 0; bit < 8; ++bit)
            if (size_t(bit) < in.size() && in[size_t(bit)] >= 0)
                sim.drive(in[size_t(bit)], s.in[size_t(7 - bit)] == '1' ? V::H : V::L);
        if (!sim.settle() || !sim.advance(stepDelay)) {
            r.failures.push_back("line " + std::to_string(s.line) + ": " + sim.lastError());
            return r;
        }
        ++r.steps;
        if (s.out.empty())
            continue;
        ++r.checked;
        std::string got;
        bool bad = false;
        for (int bit = 7; bit >= 0; --bit) {
            const V v = size_t(bit) < out.size() ? sim.value(out[size_t(bit)]) : V::Z;
            got += ch(v);
            const char want = s.out[size_t(7 - bit)];
            if ((want == '0' && v != V::L) || (want == '1' && v != V::H))
                bad = true;
        }
        if (bad)
            r.failures.push_back("line " + std::to_string(s.line) + ": ui_in " + s.in + " expected uo_out " + s.out
                                 + ", got " + got + (s.comment.empty() ? "" : "  (" + s.comment + ")"));
    }
    return r;
}

} // namespace chiply::sim
