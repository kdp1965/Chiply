// Board-level simulation (PLAN.md 6.2) on the Tiny Tapeout template and on
// small hand-built boards.
#include "core/Netlist.h"
#include "core/WokwiJson.h"
#include "sim/Simulator.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <memory>
#include <sstream>

using namespace chiply;
using namespace chiply::sim;

namespace {

struct Board {
    Document doc;
    std::unique_ptr<Netlist> nl;
    std::unique_ptr<Simulator> sim;
    explicit Board(const std::string& json)
    {
        doc = loadWokwi(json).doc;
        nl = std::make_unique<Netlist>(Netlist::build(doc, PartLibrary::builtin()));
        sim = std::make_unique<Simulator>(*nl);
    }
    V get(const char* pin) { return sim->value(*PinRef::parse(pin)); }
};

std::string templateJson()
{
    std::ifstream in(std::string(CHIPLY_REFERENCE_DIR) + "/tt_template_354858054593504257.diagram.json");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("frequency attributes")
{
    CHECK(parseFrequency("10000") == 10000.0);
    CHECK(parseFrequency("10k") == 10000.0);
    CHECK(parseFrequency("2.5kHz") == 2500.0);
    CHECK(parseFrequency("1M") == 1e6);
    CHECK_FALSE(parseFrequency("fast"));
}

TEST_CASE("template: DIP switches drive the 7-segment display through the chip")
{
    Board b(templateJson());
    INFO(b.sim->lastError());
    CHECK(b.sim->warnings().empty());
    // Open switches: the pads' pull-downs make the inputs 0, and the wires
    // show 0 too (not floating).
    CHECK(b.get("ttin:EXTIN0") == V::L);
    // All switches off: inputs read 0. IN0..3 are inverted, so
    // OUT0..3 = 1 lights segments A..D; IN4..7 pass straight, E..DP dark.
    CHECK(b.sim->segments("sevseg1") == 0x0Fu);
    CHECK(b.get("ttin:IN0") == V::L);
    // Switch 1 on: EXTIN0 = VCC, so IN0 = 1, OUT0 = 0, segment A goes dark.
    REQUIRE(b.sim->setSwitch("sw1", 0, true));
    CHECK(b.get("ttin:IN0") == V::H);
    CHECK(b.sim->segments("sevseg1") == 0x0Eu);
    // Switch 5 on: IN4 = 1 goes straight to OUT4, segment E lights.
    b.sim->setSwitch("sw1", 4, true);
    CHECK(b.sim->segments("sevseg1") == 0x1Eu);
    CHECK(b.sim->switchState("sw1", 4) == true);
    b.sim->setSwitch("sw1", 0, false);
    CHECK(b.sim->segments("sevseg1") == 0x1Fu);
}

TEST_CASE("template: RESET button against its pull-up resistor")
{
    Board b(templateJson());
    // r2 pulls EXTRST_N up to VCC (weak); the input pad passes it on.
    CHECK(b.get("ttin:EXTRST_N") == V::H);
    CHECK(b.get("ttin:RST_N") == V::H);
    // Pressing connects EXTRST_N to GND (strong), which wins.
    REQUIRE(b.sim->setPressed("btn2", true));
    CHECK(b.get("ttin:RST_N") == V::L);
    b.sim->setPressed("btn2", false);
    CHECK(b.get("ttin:RST_N") == V::H);
}

TEST_CASE("template: Step button and the clock slide switch")
{
    Board b(templateJson());
    // Slide switch starts at "1": EXTCLK goes to the Step button.
    CHECK(b.sim->switchState("sw2") == true);
    CHECK(b.get("ttin:CLK") == V::L); // button open: floating pad reads 0
    REQUIRE(b.sim->setPressed("btn1", true));
    CHECK(b.get("ttin:CLK") == V::H);
    b.sim->setPressed("btn1", false);
    CHECK(b.get("ttin:CLK") == V::L);

    // Slide to the clock generator (10 kHz in the template): 1 ms of
    // simulated time gives 10 rising edges on the chip's CLK.
    REQUIRE(b.sim->clocks().size() == 1);
    CHECK(b.sim->clocks()[0].hz == 10000.0);
    b.sim->setSwitch("sw2", 0, false);
    int edges = 0;
    V prev = b.get("ttin:CLK");
    for (int i = 0; i < 1000; ++i) { // 1 us steps
        REQUIRE(b.sim->advance(1'000'000));
        const V v = b.get("ttin:CLK");
        edges += prev == V::L && v == V::H;
        prev = v;
    }
    CHECK(edges == 10);
}

TEST_CASE("LED lights with anode high and cathode low")
{
    Board b(R"({"parts":[{"type":"wokwi-led","id":"led1","top":0,"left":0,"attrs":{}},
      {"type":"wokwi-vcc","id":"v","top":0,"left":0,"attrs":{}},{"type":"wokwi-gnd","id":"g","top":0,"left":0,"attrs":{}},
      {"type":"wokwi-pushbutton","id":"b","top":0,"left":0,"attrs":{}}],
     "connections":[["v:VCC","b:1.l","red",[]],["b:2.l","led1:A","green",[]],["led1:C","g:GND","black",[]]]})");
    CHECK(b.sim->ledLit("led1") == false);
    b.sim->setPressed("b", true);
    CHECK(b.sim->ledLit("led1") == true);
}

TEST_CASE("resistors pull weakly; strong drivers win; opposite pulls give X")
{
    Board b(R"({"parts":[{"type":"wokwi-resistor","id":"r1","top":0,"left":0,"attrs":{}},
      {"type":"wokwi-resistor","id":"r2","top":0,"left":0,"attrs":{}},
      {"type":"wokwi-gnd","id":"g","top":0,"left":0,"attrs":{}},{"type":"wokwi-vcc","id":"v","top":0,"left":0,"attrs":{}},
      {"type":"wokwi-gate-not","id":"n1","top":0,"left":0,"attrs":{}},{"type":"wokwi-gate-buffer","id":"b1","top":0,"left":0,"attrs":{}}],
     "connections":[["g:GND","r1:1","black",[]],["r1:2","n1:IN","green",[]],["b1:OUT","n1:IN","green",[]]]})");
    // b1's input floats and reads 0 (Wokwi logic), so b1 drives 0 strongly.
    CHECK(b.get("n1:IN") == V::L);
    b.sim->drive(*PinRef::parse("b1:IN"), V::H);
    b.sim->settle();
    CHECK(b.get("n1:IN") == V::H); // strong 1 beats the weak pull-down
    CHECK(b.get("n1:OUT") == V::L);

    Board pulls(R"({"parts":[{"type":"wokwi-resistor","id":"r1","top":0,"left":0,"attrs":{}},
      {"type":"wokwi-resistor","id":"r2","top":0,"left":0,"attrs":{}},
      {"type":"wokwi-gnd","id":"g","top":0,"left":0,"attrs":{}},{"type":"wokwi-vcc","id":"v","top":0,"left":0,"attrs":{}},
      {"type":"wokwi-gate-not","id":"n1","top":0,"left":0,"attrs":{}}],
     "connections":[["g:GND","r1:1","black",[]],["r1:2","n1:IN","green",[]],["v:VCC","r2:1","red",[]],["r2:2","n1:IN","green",[]]]})");
    CHECK(pulls.get("n1:IN") == V::X);
    Board down(R"({"parts":[{"type":"wokwi-resistor","id":"r1","top":0,"left":0,"attrs":{}},
      {"type":"wokwi-gnd","id":"g","top":0,"left":0,"attrs":{}},{"type":"wokwi-gate-not","id":"n1","top":0,"left":0,"attrs":{}}],
     "connections":[["g:GND","r1:1","black",[]],["r1:2","n1:IN","green",[]]]})");
    CHECK(down.get("n1:IN") == V::L); // pulled down when nothing else drives
    CHECK(down.get("n1:OUT") == V::H);
}

TEST_CASE("RESET pull-up beats the pad pull-down")
{
    Board b(templateJson());
    CHECK(b.get("ttin:EXTRST_N") == V::H); // resistor (weak) > pad pull-down
}

TEST_CASE("reference design runs on its board with the clock generator")
{
    std::ifstream in(std::string(CHIPLY_REFERENCE_DIR) + "/wokwi_414123795172381697.diagram.json");
    std::stringstream ss;
    ss << in.rdbuf();
    Board b(ss.str());
    INFO(b.sim->lastError());
    CHECK(b.sim->warnings().empty());
    // Switch the clock source to the generator and run 5 ms at 10 kHz.
    b.sim->setSwitch("sw2", 0, false);
    for (int i = 0; i < 50; ++i)
        REQUIRE(b.sim->advance(100'000'000)); // 0.1 ms
    REQUIRE(b.sim->segments("sevseg1"));
    CHECK(b.sim->now() == 5'000'000'000LL);
}
