#include "core/Netlist.h"
#include "core/WokwiJson.h"
#include "sim/Simulator.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <sstream>

using namespace chiply;
using namespace chiply::sim;

namespace {

// Builds a document from compact "type id" lines and "a:P b:Q" wires.
struct Bench {
    Document doc;
    std::unique_ptr<Netlist> nl;
    std::unique_ptr<Simulator> sim;

    // These tests check the four-state (Verilog) semantics unless an
    // option says otherwise.
    static Options verilog()
    {
        Options o;
        o.wokwiLogic = false;
        o.flopStart = FlopStart::Zero; // these tests start from a known state
        return o;
    }
    Bench(const std::string& parts, const std::string& wires, Options opt = verilog())
    {
        std::string json = R"({"parts":[)";
        std::istringstream ps(parts);
        std::string type, id;
        bool first = true;
        while (ps >> type >> id) {
            json += (first ? "" : ",") + std::string(R"({"type":")") + type + R"(","id":")" + id + R"(","top":0,"left":0,"attrs":{}})";
            first = false;
        }
        json += R"(],"connections":[)";
        std::istringstream ws(wires);
        std::string a, b;
        first = true;
        while (ws >> a >> b) {
            json += (first ? "" : ",") + std::string("[\"") + a + "\",\"" + b + "\",\"green\",[]]";
            first = false;
        }
        json += "]}";
        doc = loadWokwi(json).doc;
        nl = std::make_unique<Netlist>(Netlist::build(doc, PartLibrary::builtin()));
        sim = std::make_unique<Simulator>(*nl, opt);
    }
    void set(const char* pin, V v)
    {
        sim->drive(*PinRef::parse(pin), v);
        REQUIRE(sim->settle());
    }
    V get(const char* pin) { return sim->value(*PinRef::parse(pin)); }
    void clock(const char* pin)
    {
        set(pin, V::H);
        set(pin, V::L);
    }
};

const V L = V::L, H = V::H, X = V::X, Z = V::Z;

} // namespace

TEST_CASE("value algebra is pessimistic about X and Z")
{
    CHECK(vand(L, X) == L);
    CHECK(vand(H, X) == X);
    CHECK(vor(H, X) == H);
    CHECK(vor(L, Z) == X);
    CHECK(vxor(H, X) == X);
    CHECK(vnot(Z) == X);
    CHECK(vmux(H, H, X) == H); // both inputs agree: select does not matter
    CHECK(vmux(L, H, X) == X);
}

TEST_CASE("gate truth tables")
{
    struct G {
        const char* type;
        V t[4]; // 00 01 10 11
    };
    for (const G& g : {G{"wokwi-gate-and-2", {L, L, L, H}}, G{"wokwi-gate-or-2", {L, H, H, H}},
                       G{"wokwi-gate-xor-2", {L, H, H, L}}, G{"wokwi-gate-nand-2", {H, H, H, L}},
                       G{"wokwi-gate-nor-2", {H, L, L, L}}, G{"wokwi-gate-xnor-2", {H, L, L, H}}}) {
        INFO(g.type);
        Bench b(std::string(g.type) + " g1", "");
        for (int i = 0; i < 4; ++i) {
            b.set("g1:A", (i & 2) ? H : L);
            b.set("g1:B", (i & 1) ? H : L);
            CHECK(b.get("g1:OUT") == g.t[i]);
        }
        b.set("g1:A", Z);
        b.set("g1:B", Z);
        CHECK(b.get("g1:OUT") == X); // floating inputs are unknown
    }
}

TEST_CASE("not, buffer, mux and constants")
{
    Bench b("wokwi-gate-not n1 wokwi-gate-buffer b1 wokwi-mux-2 m1 wokwi-vcc v1 wokwi-gnd g1",
            "v1:VCC n1:IN g1:GND b1:IN v1:VCC m1:B g1:GND m1:A");
    CHECK(b.get("n1:OUT") == L);
    CHECK(b.get("b1:OUT") == L);
    b.set("m1:SEL", L);
    CHECK(b.get("m1:OUT") == L);
    b.set("m1:SEL", H);
    CHECK(b.get("m1:OUT") == H);
    b.set("m1:SEL", Z);
    CHECK(b.get("m1:OUT") == X);
}

TEST_CASE("D flip-flop samples on the rising edge only")
{
    Bench b("wokwi-flip-flop-d f1", "");
    CHECK(b.get("f1:Q") == L); // start state chosen by the test
    CHECK(b.get("f1:NOTQ") == H);
    b.set("f1:CLK", L);
    b.set("f1:D", H);
    CHECK(b.get("f1:Q") == L);
    b.set("f1:CLK", H);
    CHECK(b.get("f1:Q") == H);
    b.set("f1:D", L); // no edge
    CHECK(b.get("f1:Q") == H);
    b.set("f1:CLK", L); // falling edge
    CHECK(b.get("f1:Q") == H);
    b.clock("f1:CLK");
    CHECK(b.get("f1:Q") == L);
}

TEST_CASE("flip-flops can start unknown")
{
    Options o = Bench::verilog();
    o.flopStart = FlopStart::Unknown;
    Bench b("wokwi-flip-flop-d f1", "", o);
    CHECK(b.get("f1:Q") == X);
    b.set("f1:CLK", L);
    b.set("f1:D", H);
    b.set("f1:CLK", H);
    CHECK(b.get("f1:Q") == H);
}

TEST_CASE("asynchronous reset and set; reset wins")
{
    Bench b("wokwi-flip-flop-dsr f1 wokwi-flip-flop-dr f2", "");
    b.set("f1:CLK", L);
    b.set("f1:R", L);
    b.set("f1:S", H);
    CHECK(b.get("f1:Q") == H); // async set, no clock
    b.set("f1:R", H);
    CHECK(b.get("f1:Q") == L); // reset wins over set
    b.set("f1:S", L);
    b.set("f1:R", L);
    b.set("f1:D", H);
    b.clock("f1:CLK");
    CHECK(b.get("f1:Q") == H);

    b.set("f2:CLK", L);
    b.set("f2:R", L);
    b.set("f2:D", H);
    b.clock("f2:CLK");
    CHECK(b.get("f2:Q") == H);
    b.set("f2:R", H);
    CHECK(b.get("f2:Q") == L);
    b.clock("f2:CLK"); // reset held: clock ignored
    CHECK(b.get("f2:Q") == L);
}

TEST_CASE("SR flip-flop")
{
    Bench b("wokwi-flip-flop-sr f1", "");
    b.set("f1:CLK", L);
    b.set("f1:S", H);
    b.set("f1:R", L);
    b.clock("f1:CLK");
    CHECK(b.get("f1:Q") == H);
    b.set("f1:S", L);
    b.clock("f1:CLK");
    CHECK(b.get("f1:Q") == H); // hold
    b.set("f1:R", H);
    b.clock("f1:CLK");
    CHECK(b.get("f1:Q") == L);
}

TEST_CASE("shift register clocked through a gate has no race")
{
    // f1 -> f2, both clocked from buf1:OUT (the clock passes through a
    // gate, so it reaches the flops one delta after the clock net). f2 must
    // still capture f1's old value.
    Bench b("wokwi-flip-flop-d f1 wokwi-flip-flop-d f2 wokwi-gate-buffer cb wokwi-gate-buffer cb2",
            "f1:Q f2:D cb:OUT f1:CLK cb:OUT cb2:IN cb2:OUT f2:CLK");
    b.set("cb:IN", L);
    b.set("f1:D", H);
    b.clock("cb:IN");
    CHECK(b.get("f1:Q") == H);
    CHECK(b.get("f2:Q") == L); // not H: f2 saw f1's old Q
    b.clock("cb:IN");
    CHECK(b.get("f2:Q") == H);
}

TEST_CASE("two drivers that disagree give X")
{
    Bench b("wokwi-vcc v1 wokwi-gnd g1 wokwi-gate-not n1", "v1:VCC n1:IN g1:GND n1:IN");
    CHECK(b.get("n1:IN") == X);
    CHECK(b.get("n1:OUT") == X);
}

TEST_CASE("a ring oscillator is reported, not hung")
{
    // NAND with enable + two inverters: with EN low the ring holds a known
    // value; with EN high it oscillates forever in zero-delay mode.
    Bench b("wokwi-gate-nand-2 n1 wokwi-gate-not n2 wokwi-gate-not n3", "n1:OUT n2:IN n2:OUT n3:IN n3:OUT n1:B");
    b.set("n1:A", L);
    CHECK(b.get("n1:OUT") == H);
    b.sim->drive(*PinRef::parse("n1:A"), H);
    CHECK_FALSE(b.sim->settle());
    CHECK(b.sim->lastError().find("did not settle") != std::string::npos);
}

TEST_CASE("a ring without a known value stays X, as in Verilog")
{
    Bench b("wokwi-gate-not n1 wokwi-gate-not n2 wokwi-gate-not n3", "n1:OUT n2:IN n2:OUT n3:IN n3:OUT n1:IN");
    CHECK(b.sim->lastError().empty());
    CHECK(b.get("n1:OUT") == X);
}

TEST_CASE("unit-delay mode shows the delay")
{
    Options o = Bench::verilog();
    o.gateDelay = 100; // ps
    Bench b("wokwi-gate-not n1 wokwi-gate-not n2", "n1:OUT n2:IN", o);
    b.sim->drive(*PinRef::parse("n1:IN"), L);
    b.sim->advance(1000);
    CHECK(b.get("n2:OUT") == L);
    b.sim->drive(*PinRef::parse("n1:IN"), H);
    b.sim->advance(150); // first gate switched, second not yet
    CHECK(b.get("n1:OUT") == L);
    CHECK(b.get("n2:OUT") == L);
    b.sim->advance(100);
    CHECK(b.get("n2:OUT") == H);
}

TEST_CASE("Wokwi logic: floating and unknown inputs read 0")
{
    Options w; // default: Wokwi logic
    Bench b("wokwi-gate-not n1 wokwi-gate-and-2 a1", "", w);
    CHECK(b.get("n1:IN") == Z);  // the net itself is still shown as floating
    CHECK(b.get("n1:OUT") == H); // but the gate reads it as 0
    CHECK(b.get("a1:OUT") == L);
}

TEST_CASE("Wokwi logic: a cross-coupled latch starts from definite values")
{
    // SR latch from two NOR gates. With four-state logic it starts X and
    // stays X until set or reset; with Wokwi logic it starts at a definite
    // state, like on wokwi.com, so designs relying on that run.
    const char* parts = "wokwi-gate-nor-2 n1 wokwi-gate-nor-2 n2";
    const char* wires = "n1:OUT n2:A n2:OUT n1:B";
    Bench verilog(parts, wires);
    CHECK(verilog.get("n1:OUT") == X);
    Options w;
    Bench wokwi(parts, wires, w);
    CHECK(known(wokwi.get("n1:OUT")));
    CHECK(wokwi.get("n2:OUT") == vnot(wokwi.get("n1:OUT")));
    wokwi.set("n1:A", H); // set -> n1:OUT = 0, n2:OUT = 1
    CHECK(wokwi.get("n1:OUT") == L);
    CHECK(wokwi.get("n2:OUT") == H);
}

TEST_CASE("SR flip-flop toggles with S and R both set (Wokwi)")
{
    Bench b("wokwi-flip-flop-sr f1", "");
    b.set("f1:CLK", L);
    b.set("f1:S", H);
    b.set("f1:R", H);
    const V q0 = b.get("f1:Q");
    b.clock("f1:CLK");
    CHECK(b.get("f1:Q") == vnot(q0));
    b.clock("f1:CLK");
    CHECK(b.get("f1:Q") == q0);
}

TEST_CASE("flip-flops start random like Wokwi; a seed makes it repeatable")
{
    // 64 flip-flops: random start gives both values; the same seed gives
    // the same state.
    std::string parts;
    for (int i = 0; i < 64; ++i)
        parts += "wokwi-flip-flop-d f" + std::to_string(i) + " ";
    Options o;
    o.seed = 42;
    Bench a(parts, "", o), b(parts, "", o);
    int ones = 0;
    for (int i = 0; i < 64; ++i) {
        const std::string q = "f" + std::to_string(i) + ":Q";
        ones += a.get(q.c_str()) == H;
        CHECK(a.get(q.c_str()) == b.get(q.c_str()));
    }
    CHECK(ones > 8);
    CHECK(ones < 56);
}
