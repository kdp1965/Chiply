#include "core/Netlist.h"
#include "core/WokwiJson.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>

using namespace chiply;

namespace {

std::string readRef(const std::string& name)
{
    std::ifstream in(std::string(CHIPLY_REFERENCE_DIR) + "/" + name, std::ios::binary);
    REQUIRE(in);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Document parse(const std::string& json) { return loadWokwi(json).doc; }

} // namespace

TEST_CASE("wires and junctions join pins into nets")
{
    Document d = parse(R"({"parts":[
      {"type":"wokwi-gate-and-2","id":"and1","top":0,"left":0,"attrs":{}},
      {"type":"wokwi-gate-not","id":"not1","top":0,"left":200,"attrs":{}},
      {"type":"wokwi-gate-not","id":"not2","top":100,"left":200,"attrs":{}},
      {"type":"wokwi-junction","id":"j1","top":0,"left":150,"attrs":{}}],
     "connections":[["and1:OUT","j1:J","green",[]],["j1:J","not1:IN","green",[]],["j1:J","not2:IN","green",[]]]})");
    Netlist nl = Netlist::build(d, PartLibrary::builtin());
    CHECK(nl.warnings.empty());
    const int n = nl.netOf({"and1", "OUT"});
    CHECK(n >= 0);
    CHECK(nl.netOf({"not1", "IN"}) == n);
    CHECK(nl.netOf({"not2", "IN"}) == n);
    CHECK(nl.nets[size_t(n)].drivers.size() == 1);
    CHECK(nl.nets[size_t(n)].loads.size() == 2);
    CHECK(nl.nets[size_t(n)].name == "and1:OUT");
    // Unconnected pins still get their own nets.
    CHECK(nl.netOf({"and1", "A"}) != nl.netOf({"and1", "B"}));
}

TEST_CASE("pins with the same prefix are connected inside the part; switches are not")
{
    Document d = parse(R"({"parts":[
      {"type":"wokwi-pushbutton","id":"btn1","top":0,"left":0,"attrs":{}},
      {"type":"wokwi-slide-switch","id":"sw1","top":0,"left":100,"attrs":{}},
      {"type":"wokwi-7segment","id":"seg1","top":0,"left":200,"attrs":{}}],"connections":[]})");
    Netlist nl = Netlist::build(d, PartLibrary::builtin());
    CHECK(nl.netOf({"btn1", "1.l"}) == nl.netOf({"btn1", "1.r"}));
    CHECK(nl.netOf({"btn1", "2.l"}) == nl.netOf({"btn1", "2.r"}));
    CHECK(nl.netOf({"btn1", "1.l"}) != nl.netOf({"btn1", "2.l"})); // the switch itself is open
    CHECK(nl.netOf({"sw1", "1"}) != nl.netOf({"sw1", "2"}));
    CHECK(nl.netOf({"seg1", "COM.1"}) == nl.netOf({"seg1", "COM.2"}));
}

TEST_CASE("wires to unknown pins and parts are reported, not lost")
{
    Document d = parse(R"({"parts":[
      {"type":"wokwi-gate-not","id":"not1","top":0,"left":0,"attrs":{}},
      {"type":"x-mystery","id":"m1","top":0,"left":100,"attrs":{}}],
     "connections":[["not1:OUT","m1:Q","green",[]],["not1:BOGUS","m1:Q","green",[]],["not1:IN","nope:A","green",[]]]})");
    Netlist nl = Netlist::build(d, PartLibrary::builtin());
    CHECK(nl.warnings.size() == 3); // unknown type, unknown pin, missing part
    CHECK(nl.netOf({"m1", "Q"}) == nl.netOf({"not1", "OUT"}));
    CHECK(nl.netOf({"not1", "BOGUS"}) == nl.netOf({"not1", "OUT"}));
}

// The reference check: Chiply's nets must partition the chip-side pins exactly
// as Wokwi's own Verilog export of the same design does.
TEST_CASE("netlist matches Wokwi's Verilog export of the reference design")
{
    const Document doc = parse(readRef("wokwi_414123795172381697.diagram.json"));
    const PartLibrary& lib = PartLibrary::builtin();
    Netlist nl = Netlist::build(doc, lib);
    CHECK(nl.warnings.empty());

    const std::string v = readRef("tt_um_wokwi_414123795172381697.v");
    std::map<std::string, std::string> constOf; // wokwi net -> "0"/"1"
    std::vector<std::pair<PinRef, std::string>> pins; // our pin, wokwi net

    // Bidirectional blocks by verilogBit.
    std::map<std::string, std::string> uioPart;
    for (const Part& p : doc.parts)
        if (p.type == "board-tt-block-bidirectional-io")
            uioPart[p.attrs.value("verilogBit", std::string())] = p.id;
    auto ttPin = [&](const std::string& sig, const std::string& idx) -> std::optional<PinRef> {
        if (sig == "clk") return PinRef{"ttin", "CLK"};
        if (sig == "rst_n") return PinRef{"ttin", "RST_N"};
        if (sig == "ui_in") return PinRef{"ttin", "IN" + idx};
        if (sig == "uo_out") return PinRef{"ttout", "OUT" + idx};
        if (sig == "uio_in" && uioPart.count(idx)) return PinRef{uioPart[idx], "IN"};
        if (sig == "uio_out" && uioPart.count(idx)) return PinRef{uioPart[idx], "OUT"};
        if (sig == "uio_oe" && uioPart.count(idx)) return PinRef{uioPart[idx], "OE"};
        return std::nullopt;
    };

    const std::regex wireRe(R"(wire\s+(net\d+)\s*=\s*([^;]+);)");
    for (std::sregex_iterator it(v.begin(), v.end(), wireRe), end; it != end; ++it) {
        const std::string net = (*it)[1], rhs = (*it)[2];
        std::smatch m;
        if (rhs == "1'b0" || rhs == "1'b1")
            constOf[net] = rhs.substr(3);
        else if (std::regex_match(rhs, m, std::regex(R"((\w+)\[(\d+)\])")))
            pins.emplace_back(*ttPin(m[1], m[2]), net);
        else if (auto p = ttPin(rhs, ""))
            pins.emplace_back(*p, net);
    }
    const std::regex assignRe(R"(assign\s+(\w+)\[(\d+)\]\s*=\s*(net\d+);)");
    for (std::sregex_iterator it(v.begin(), v.end(), assignRe), end; it != end; ++it)
        if (auto p = ttPin((*it)[1], (*it)[2]))
            pins.emplace_back(*p, (*it)[3]);

    // Cell instances: map Verilog ports back to Wokwi pin names.
    const std::regex instRe(R"((\w+_cell)\s+(\w+)\s*\(([^;]*)\);)");
    const std::regex portRe(R"(\.(\w+)\s*\(\s*(net\d+)?\s*\))");
    int instances = 0;
    for (std::sregex_iterator it(v.begin(), v.end(), instRe), end; it != end; ++it, ++instances) {
        const std::string id = (*it)[2], body = (*it)[3];
        const Part* part = doc.findPart(id);
        REQUIRE(part);
        const PartDef* def = lib.find(part->type);
        REQUIRE(def);
        std::map<std::string, std::string> pinOfPort;
        for (auto p = def->verilog["ports"].begin(); p != def->verilog["ports"].end(); ++p)
            pinOfPort[p.value().get<std::string>()] = p.key();
        for (std::sregex_iterator pt(body.begin(), body.end(), portRe); pt != std::sregex_iterator(); ++pt) {
            if (!(*pt)[2].matched)
                continue; // unconnected output, e.g. .notq ()
            REQUIRE(pinOfPort.count((*pt)[1]));
            pins.emplace_back(PinRef{id, pinOfPort[(*pt)[1]]}, (*pt)[2]);
        }
    }
    CHECK(instances == 924); // 325 AND + 230 OR + 207 DFF + 62 MUX + 49 DSR + 27 XOR + 23 NOT + 1 NAND
    REQUIRE(pins.size() > 2500);

    std::map<std::string, int> oursOf;   // wokwi net -> our net
    std::map<int, std::string> theirsOf; // our net -> wokwi net
    int mismatches = 0;
    for (const auto& [ref, wnet] : pins) {
        const int ours = nl.netOf(ref);
        REQUIRE(ours >= 0);
        auto [a, newA] = oursOf.emplace(wnet, ours);
        auto [b, newB] = theirsOf.emplace(ours, wnet);
        if (a->second != ours || b->second != wnet) {
            ++mismatches;
            UNSCOPED_INFO(ref.str() << ": ours " << nl.nets[size_t(ours)].name << ", Wokwi " << wnet);
        }
    }
    CHECK(mismatches == 0);

    // Constant nets contain the matching power symbol.
    for (const auto& [wnet, value] : constOf) {
        if (!oursOf.count(wnet))
            continue;
        const Net& n = nl.nets[size_t(oursOf[wnet])];
        bool found = false;
        for (const NetPin& np : n.drivers) {
            const Device& d = nl.devices[size_t(np.device)];
            found |= (value == "0" && d.type == "wokwi-gnd") || (value == "1" && d.type == "wokwi-vcc");
        }
        INFO(wnet << " = " << value << ", our net " << n.name);
        CHECK(found);
    }
}
