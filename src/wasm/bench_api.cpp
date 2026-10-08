// The engine's benchmark for a web page (PLAN.md 13): plain C entry points
// for the JavaScript side. Built only with Emscripten (see src/wasm/CMakeLists.txt).
#include "sim/Bench.h"

#include <emscripten/emscripten.h>

#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {

// Runs the benchmark on a diagram's JSON; returns a JSON report (free it
// with chiply_free). `switches`: "part,index,on;..." or "".
EMSCRIPTEN_KEEPALIVE char* chiply_bench(const char* json, double seconds, int cycles, const char* switches)
{
    chiply::sim::BenchOptions o;
    o.boardSeconds = seconds;
    o.randomCycles = cycles;
    std::string sw = switches ? switches : "";
    std::size_t pos = 0;
    while (pos < sw.size()) {
        const std::size_t end = sw.find(';', pos);
        const std::string item = sw.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        const std::size_t c1 = item.find(','), c2 = item.rfind(',');
        if (c1 != std::string::npos && c2 != std::string::npos && c2 > c1)
            o.switches.push_back({item.substr(0, c1), std::atoi(item.substr(c1 + 1, c2 - c1 - 1).c_str()),
                                  item.substr(c2 + 1) == "1"});
        if (end == std::string::npos)
            break;
        pos = end + 1;
    }
    const chiply::sim::BenchResult r = chiply::sim::runBench(json ? json : "", o);
    const std::string out = r.json();
    char* buf = static_cast<char*>(std::malloc(out.size() + 1));
    std::memcpy(buf, out.c_str(), out.size() + 1);
    return buf;
}

EMSCRIPTEN_KEEPALIVE void chiply_free(char* p) { std::free(p); }

} // extern "C"
