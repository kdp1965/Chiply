// chiply-cli: headless access to the Chiply core.
//
//   chiply-cli info   <diagram.json>            summary of parts and wires
//   chiply-cli format <diagram.json> [out.json] load and save (round trip)
//   chiply-cli check-roundtrip <diagram.json>   exit 0 if save == input bytes
//   chiply-cli netlist <diagram.json> [--nets]  connectivity summary (or every net)
//   chiply-cli sim <diagram.json> <script>      run a stimulus script (see simScript)
#include "core/Netlist.h"
#include "core/WokwiJson.h"
#include "sim/Simulator.h"

#include <chrono>

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

using namespace chiply;

namespace {

int simScript(const std::string& diagram, const std::string& scriptPath)
{
    using namespace chiply::sim;
    LoadResult r = loadWokwiFile(diagram);
    const Netlist nl = Netlist::build(r.doc, PartLibrary::builtin());
    std::ifstream script(scriptPath);
    if (!script)
        throw LoadError("cannot open " + scriptPath);
    std::vector<std::string> lines;
    for (std::string l; std::getline(script, l);)
        lines.push_back(l);
    Options opt;
    for (const std::string& l : lines)
        if (l.rfind("option x-start", 0) == 0)
            opt.flopsStartUnknown = true;
        else if (l.rfind("option chip-only", 0) == 0)
            opt.board = false;
        else if (l.rfind("option verilog", 0) == 0)
            opt.wokwiLogic = false;
    Simulator sim(nl, opt);
    for (const std::string& w : sim.warnings())
        std::cerr << "warning: " << w << "\n";

    auto value = [](const std::string& s, V* v) {
        if (s == "0") *v = V::L;
        else if (s == "1") *v = V::H;
        else if (s == "x" || s == "X") *v = V::X;
        else if (s == "z" || s == "Z") *v = V::Z;
        else return false;
        return true;
    };
    int failures = 0;
    for (std::size_t n = 0; n < lines.size(); ++n) {
        std::istringstream in(lines[n].substr(0, lines[n].find('#')));
        std::string cmd;
        if (!(in >> cmd) || cmd == "option")
            continue;
        const std::string where = scriptPath + ":" + std::to_string(n + 1) + ": ";
        auto pin = [&](const std::string& s) {
            auto ref = PinRef::parse(s);
            if (!ref || sim.netOf(*ref) < 0)
                throw std::runtime_error(where + "unknown pin \"" + s + "\"");
            return *ref;
        };
        auto ok = [&](bool settled) {
            if (!settled)
                throw std::runtime_error(where + sim.lastError());
        };
        std::string a, b;
        V v;
        if (cmd == "set" && in >> a >> b && value(b, &v)) {
            sim.drive(pin(a), v);
            ok(sim.settle());
        } else if (cmd == "clock" && in >> a) {
            int count = 1;
            in >> count;
            const PinRef p = pin(a);
            for (int i = 0; i < count; ++i) {
                sim.drive(p, V::L);
                ok(sim.settle());
                sim.drive(p, V::H);
                ok(sim.settle());
                sim.drive(p, V::L);
                ok(sim.settle());
            }
        } else if (cmd == "run" && in >> a) {
            ok(sim.advance(std::stoll(a)));
        } else if (cmd == "expect" && in >> a >> b && value(b, &v)) {
            const V got = sim.value(pin(a));
            if (got != v) {
                ++failures;
                std::cout << where << "expected " << a << " = " << toChar(v) << ", got " << toChar(got) << "\n";
            }
        } else if ((cmd == "press" || cmd == "release") && in >> a) {
            if (!sim.setPressed(a, cmd == "press"))
                throw std::runtime_error(where + "no pushbutton \"" + a + "\"");
        } else if (cmd == "switch" && in >> a >> b) {
            std::string on;
            in >> on;
            if (!sim.setSwitch(a, std::stoi(b), on == "1"))
                throw std::runtime_error(where + "no switch \"" + a + "\" index " + b);
        } else if (cmd == "segments" && in >> a >> b) {
            auto got = sim.segments(a);
            if (!got)
                throw std::runtime_error(where + "no 7-segment display \"" + a + "\"");
            const unsigned want = unsigned(std::stoul(b, nullptr, 16));
            if (*got != want) {
                ++failures;
                std::cout << where << "expected segments of " << a << " = " << std::hex << want << ", got " << *got
                          << std::dec << "\n";
            }
        } else if (cmd == "print") {
            while (in >> a)
                std::cout << a << " = " << toChar(sim.value(pin(a))) << "  ";
            std::cout << "\n";
        } else {
            throw std::runtime_error(where + "cannot parse \"" + lines[n] + "\"");
        }
    }
    if (failures)
        std::cout << failures << " expectation(s) failed\n";
    return failures ? 1 : 0;
}

int usage()
{
    std::cerr << "usage:\n"
                 "  chiply-cli info <diagram.json>\n"
                 "  chiply-cli format <diagram.json> [out.json]\n"
                 "  chiply-cli check-roundtrip <diagram.json>\n"
                 "  chiply-cli netlist <diagram.json> [--nets]\n"
                 "  chiply-cli sim <diagram.json> <script>\n"
                 "\n"
                 "sim script, one command per line (# comments):\n"
                 "  option x-start        flip-flops start unknown (default: 0, like Wokwi)\n"
                 "  option chip-only      Tiny Tapeout blocks passive: drive IN*/CLK/RST_N directly\n"
                 "  option verilog        four-state logic (X propagates); default is Wokwi logic\n"
                 "  set <part:PIN> <0|1|x|z>\n"
                 "  clock <part:PIN> [n]  n rising edges (default 1), 0-1-0 each\n"
                 "  run <ps>              advance simulated time\n"
                 "  expect <part:PIN> <0|1|x|z>\n"
                 "  press <button> / release <button>\n"
                 "  switch <part> <index> <0|1>   DIP switch index 0..7; slide switch index 0\n"
                 "  segments <7seg> <hex>          expect lit segments (bit 0 = A .. bit 7 = DP)\n"
                 "  print <part:PIN>...\n";
    return 2;
}

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw LoadError("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
        return usage();
    const std::string cmd = argv[1];
    const std::string path = argv[2];
    try {
        if (cmd == "info") {
            LoadResult r = loadWokwiFile(path);
            std::map<std::string, int> byType;
            for (const Part& p : r.doc.parts)
                ++byType[p.type];
            std::cout << path << ": " << r.doc.parts.size() << " parts, " << r.doc.wires.size()
                      << " wires, author \"" << r.doc.author() << "\"\n";
            for (const auto& [t, n] : byType)
                std::cout << "  " << n << "\t" << t << "\n";
            for (const std::string& w : r.warnings)
                std::cout << "warning: " << w << "\n";
            return 0;
        }
        if (cmd == "format") {
            LoadResult r = loadWokwiFile(path);
            if (argc > 3)
                saveWokwiFile(r.doc, argv[3]);
            else
                std::cout << saveWokwi(r.doc);
            return 0;
        }
        if (cmd == "sim") {
            if (argc < 4)
                return usage();
            return simScript(path, argv[3]);
        }
        if (cmd == "netlist") {
            LoadResult r = loadWokwiFile(path);
            const auto t0 = std::chrono::steady_clock::now();
            Netlist nl = Netlist::build(r.doc, PartLibrary::builtin());
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
            std::size_t multi = 0, undriven = 0;
            for (const Net& n : nl.nets) {
                multi += n.drivers.size() > 1;
                undriven += n.drivers.empty() && !n.loads.empty();
            }
            std::cout << path << ": " << nl.devices.size() << " devices, " << nl.nets.size() << " nets ("
                      << nl.connectedNetCount() << " connected), built in " << us / 1000.0 << " ms\n"
                      << "  nets with several drivers: " << multi << "\n"
                      << "  nets with loads but no driver: " << undriven
                      << " (inputs fed by switches/buttons count here until simulation)\n";
            for (const std::string& w : nl.warnings)
                std::cout << "warning: " << w << "\n";
            if (argc > 3 && std::string(argv[3]) == "--nets") {
                for (const Net& n : nl.nets) {
                    if (n.pins.size() < 2)
                        continue;
                    std::cout << n.name << ":";
                    for (const NetPin& np : n.pins)
                        std::cout << " " << nl.devices[size_t(np.device)].partId << ":"
                                  << nl.devices[size_t(np.device)].pinNames[size_t(np.pin)];
                    std::cout << "\n";
                }
            }
            return 0;
        }
        if (cmd == "check-roundtrip") {
            const std::string in = readFile(path);
            const std::string out = saveWokwi(loadWokwi(in).doc);
            if (in == out) {
                std::cout << "identical (" << in.size() << " bytes)\n";
                return 0;
            }
            std::size_t i = 0;
            while (i < in.size() && i < out.size() && in[i] == out[i])
                ++i;
            std::size_t line = 1 + std::count(in.begin(), in.begin() + i, '\n');
            std::cout << "differs at byte " << i << " (line " << line << ")\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return usage();
}
