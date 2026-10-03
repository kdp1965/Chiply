// chiply-cli: headless access to the Chiply core.
//
//   chiply-cli info   <diagram.json>            summary of parts and wires
//   chiply-cli format <diagram.json> [out.json] load and save (round trip)
//   chiply-cli check-roundtrip <diagram.json>   exit 0 if save == input bytes
//   chiply-cli netlist <diagram.json> [--nets]  connectivity summary (or every net)
//   chiply-cli sim <diagram.json> <script>      run a stimulus script (see simScript)
//   chiply-cli truthtable <diagram.json> <truthtable.md> [options]
//                                                check a Tiny Tapeout truth table
#include "core/Netlist.h"
#include "core/WokwiJson.h"
#include "sim/Simulator.h"
#include "sim/Trace.h"
#include "sim/TruthTable.h"

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
            opt.flopStart = FlopStart::Unknown;
        else if (l.rfind("option zero-start", 0) == 0)
            opt.flopStart = FlopStart::Zero;
        else if (l.rfind("option seed ", 0) == 0)
            opt.seed = std::stoull(l.substr(12));
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
    Trace trace;
    trace.addLogicAnalyzers(sim);
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
        } else if (cmd == "trace" && in >> a) {
            std::string name;
            in >> name;
            trace.add(sim, "probes", name.empty() ? a : name, sim.netOf(pin(a)));
        } else if (cmd == "vcd" && in >> a) {
            trace.collect(sim);
            std::ofstream f(a, std::ios::binary);
            if (!f)
                throw std::runtime_error(where + "cannot write " + a);
            trace.writeVcd(f, sim.now());
            std::cout << "wrote " << a << " (" << trace.channels().size() << " signals)\n";
        } else if (cmd == "print") {
            while (in >> a)
                std::cout << a << " = " << toChar(sim.value(pin(a))) << "  ";
            std::cout << "\n";
        } else {
            throw std::runtime_error(where + "cannot parse \"" + lines[n] + "\"");
        }
        trace.collect(sim);
    }
    if (failures)
        std::cout << failures << " expectation(s) failed\n";
    return failures ? 1 : 0;
}

std::string readFile(const std::string& path);

int truthTable(int argc, char** argv)
{
    using namespace chiply::sim;
    const std::string diagram = argv[2], tablePath = argv[3];
    Options opt;
    opt.board = false; // like tt-support-tools: the testbench drives ui_in directly
    std::vector<std::pair<std::string, V>> sets;
    std::string vcdPath;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--verilog")
            opt.wokwiLogic = false;
        else if (a == "--zero-start")
            opt.flopStart = FlopStart::Zero;
        else if (a == "--x-start")
            opt.flopStart = FlopStart::Unknown;
        else if (a == "--seed" && i + 1 < argc)
            opt.seed = std::stoull(argv[++i]);
        else if (a == "--vcd" && i + 1 < argc)
            vcdPath = argv[++i];
        else if (a == "--set" && i + 1 < argc) {
            const std::string kv = argv[++i];
            const auto eq = kv.find('=');
            const std::string v = eq == std::string::npos ? "" : kv.substr(eq + 1);
            if (v != "0" && v != "1")
                throw std::runtime_error("--set wants part:PIN=0 or part:PIN=1, got \"" + kv + "\"");
            sets.push_back({kv.substr(0, eq), v == "1" ? V::H : V::L});
        } else
            throw std::runtime_error("unknown option " + a);
    }
    LoadResult r = loadWokwiFile(diagram);
    const Netlist nl = Netlist::build(r.doc, PartLibrary::builtin());
    Simulator sim(nl, opt);
    std::vector<int> in(8, -1), out(8, -1);
    std::string inId, outId;
    for (const Device& d : nl.devices) {
        if (inId.empty() && (d.type == "board-tt-block-input" || d.type == "board-tt-block-input-8"))
            inId = d.partId;
        if (outId.empty() && d.type == "board-tt-block-output")
            outId = d.partId;
    }
    if (inId.empty() || outId.empty())
        throw std::runtime_error("the design needs a Tiny Tapeout input block and output block");
    for (int b = 0; b < 8; ++b) {
        in[size_t(b)] = sim.netOf(PinRef{inId, "IN" + std::to_string(b)});
        out[size_t(b)] = sim.netOf(PinRef{outId, "OUT" + std::to_string(b)});
    }
    for (const auto& [pinName, v] : sets) {
        auto ref = PinRef::parse(pinName);
        if (!ref || sim.netOf(*ref) < 0)
            throw std::runtime_error("unknown pin \"" + pinName + "\"");
        sim.drive(*ref, v);
    }
    sim.settle();
    Trace trace;
    if (!vcdPath.empty()) {
        for (int b = 0; b < 8; ++b) {
            trace.add(sim, "tt", "ui_in" + std::to_string(b), in[size_t(b)]);
            trace.add(sim, "tt", "uo_out" + std::to_string(b), out[size_t(b)]);
        }
        trace.addLogicAnalyzers(sim);
    }
    const TruthTable table = parseTruthTable(readFile(tablePath));
    for (const std::string& w : table.warnings)
        std::cerr << tablePath << ": warning: " << w << "\n";
    if (table.steps.empty())
        throw std::runtime_error(tablePath + ": no truth table rows found");
    const TruthResult res = runTruthTable(sim, table, in, out);
    for (const std::string& f : res.failures)
        std::cout << tablePath << ":" << f.substr(5) << "\n"; // "line N: ..." -> "path:N: ..."
    if (!vcdPath.empty()) {
        trace.collect(sim);
        std::ofstream f(vcdPath, std::ios::binary);
        trace.writeVcd(f, sim.now());
    }
    std::cout << res.steps << " steps, " << res.checked << " checked, " << res.failures.size() << " failed\n";
    return res.failures.empty() ? 0 : 1;
}

int usage()
{
    std::cerr << "usage:\n"
                 "  chiply-cli info <diagram.json>\n"
                 "  chiply-cli format <diagram.json> [out.json]\n"
                 "  chiply-cli check-roundtrip <diagram.json>\n"
                 "  chiply-cli netlist <diagram.json> [--nets]\n"
                 "  chiply-cli sim <diagram.json> <script>\n"
                 "  chiply-cli truthtable <diagram.json> <truthtable.md> [--set part:PIN=0|1]... [--vcd out.vcd]\n"
                 "             [--verilog] [--zero-start|--x-start] [--seed n]\n"
                 "     Tiny Tapeout truthtable.md: rows | ui_in | uo_out | comment |, 8 chars MSB first;\n"
                 "     inputs 0 1 t(oggle) c(lock) x/- (unchanged), outputs 0 1 x/- (don't care).\n"
                 "     Chip only: ui_in drives the input block's IN pins; CLK and RST_N float unless --set.\n"
                 "\n"
                 "sim script, one command per line (# comments):\n"
                 "  option x-start        flip-flops start unknown (default: random, like Wokwi)\n"
                 "  option zero-start     flip-flops start at 0\n"
                 "  option seed <n>       repeatable random start state\n"
                 "  option chip-only      Tiny Tapeout blocks passive: drive IN*/CLK/RST_N directly\n"
                 "  option verilog        four-state logic (X propagates); default is Wokwi logic\n"
                 "  set <part:PIN> <0|1|x|z>\n"
                 "  clock <part:PIN> [n]  n rising edges (default 1), 0-1-0 each\n"
                 "  run <ps>              advance simulated time\n"
                 "  expect <part:PIN> <0|1|x|z>\n"
                 "  press <button> / release <button>\n"
                 "  switch <part> <index> <0|1>   DIP switch index 0..7; slide switch index 0\n"
                 "  segments <7seg> <hex>          expect lit segments (bit 0 = A .. bit 7 = DP)\n"
                 "  print <part:PIN>...\n"
                 "  trace <part:PIN> [name]       record a net (logic analyzers are recorded too)\n"
                 "  vcd <file>                    write what was recorded so far as VCD\n";
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
        if (cmd == "truthtable") {
            if (argc < 4)
                return usage();
            return truthTable(argc, argv);
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
