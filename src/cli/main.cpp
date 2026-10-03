// chiply-cli: headless access to the Chiply core.
//
//   chiply-cli info   <diagram.json>            summary of parts and wires
//   chiply-cli format <diagram.json> [out.json] load and save (round trip)
//   chiply-cli check-roundtrip <diagram.json>   exit 0 if save == input bytes
//   chiply-cli netlist <diagram.json> [--nets]  connectivity summary (or every net)
#include "core/Netlist.h"
#include "core/WokwiJson.h"

#include <chrono>

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

using namespace chiply;

namespace {

int usage()
{
    std::cerr << "usage:\n"
                 "  chiply-cli info <diagram.json>\n"
                 "  chiply-cli format <diagram.json> [out.json]\n"
                 "  chiply-cli check-roundtrip <diagram.json>\n"
                 "  chiply-cli netlist <diagram.json> [--nets]\n";
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
